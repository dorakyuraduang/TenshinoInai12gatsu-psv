#pragma once
#include "media.hpp"
#include <SDL.h>
#include <memory>
namespace tenshi {
// A bounded, seekable WAV view of one original archive entry. No extracted file.
struct ArchiveWave {
    std::ifstream file;
    Bytes header;
    Sint64 base = 0, pos = 0, length = 0;
    static ArchiveWave &get(SDL_RWops *rw) {
        return *static_cast<ArchiveWave *>(rw->hidden.unknown.data1);
    }
    static Sint64 size(SDL_RWops *rw) {
        return get(rw).length;
    }
    static Sint64 seek(SDL_RWops *rw, Sint64 offset, int whence) {
        auto &s = get(rw);
        Sint64 base = whence == RW_SEEK_SET   ? 0
                      : whence == RW_SEEK_CUR ? s.pos
                      : whence == RW_SEEK_END ? s.length
                                              : -1;
        if (base < 0 || offset < -base || offset > s.length - base)
            return SDL_SetError("Seek outside audio entry"), -1;
        s.pos = base + offset;
        return s.pos;
    }
    static size_t read(SDL_RWops *rw, void *dst, size_t unit, size_t count) {
        auto &s = get(rw);
        if (!unit || !count || unit > size_t(s.length - s.pos))
            return 0;
        size_t n = std::min(size_t(s.length - s.pos) / unit, count), bytes = n * unit, done = 0;
        auto *out = static_cast<uint8_t *>(dst);
        if (s.pos < 44) {
            size_t take = std::min(bytes, size_t(44 - s.pos));
            std::memcpy(out, s.header.data() + s.pos, take);
            done += take;
            s.pos += take;
        }
        if (done < bytes) {
            s.file.clear();
            s.file.seekg(std::streamoff(s.base + s.pos - 44));
            if (!s.file)
                return SDL_SetError("Audio archive seek failed"), done / unit;
            s.file.read(reinterpret_cast<char *>(out + done), std::streamsize(bytes - done));
            size_t got = size_t(s.file.gcount());
            s.pos += got;
            done += got;
            if (done < bytes)
                SDL_SetError("Audio archive read failed");
        }
        return done / unit;
    }
    static size_t write(SDL_RWops *, const void *, size_t, size_t) {
        return 0;
    }
    static int close(SDL_RWops *rw) {
        delete &get(rw);
        SDL_FreeRW(rw);
        return 0;
    }
    static SDL_RWops *open(const Archive &archive, const std::string &name) {
        auto e = archive.entry(name);
        auto s = std::make_unique<ArchiveWave>();
        s->file.open(archive.path, std::ios::binary);
        if (!s->file)
            throw std::runtime_error("Cannot open audio archive: " + archive.path);
        Bytes head(18);
        if (e.size < 18)
            throw std::runtime_error("Truncated W audio entry");
        s->file.seekg(std::streamoff(e.offset));
        if (!s->file.read(reinterpret_cast<char *>(head.data()), 18))
            throw std::runtime_error("Cannot read W audio header");
        s->header = waveHeader(head, e.size);
        s->base = Sint64(e.offset) + 18;
        s->length = 44 + Sint64(u32(s->header, 40));
        auto *rw = SDL_AllocRW();
        if (!rw)
            throw std::runtime_error("Cannot allocate audio stream");
        rw->size = size;
        rw->seek = seek;
        rw->read = read;
        rw->write = write;
        rw->close = close;
        rw->type = SDL_RWOPS_UNKNOWN;
        rw->hidden.unknown.data1 = s.release();
        rw->hidden.unknown.data2 = nullptr;
        return rw;
    }
};
// Decode only the outgoing overlap, keeping the main track streamed by SDL_mixer.
inline Bytes musicOverlap(const Archive &archive, const std::string &name, double position, double seconds,
                          bool loop) {
    std::unique_ptr<SDL_RWops, int (*)(SDL_RWops *)> rw(ArchiveWave::open(archive, name), SDL_RWclose);
    Bytes header(44);
    if (SDL_RWread(rw.get(), header.data(), 1, header.size()) != header.size())
        throw std::runtime_error("Cannot read music overlap header");
    const uint32_t block = u16(header, 32), rate = u32(header, 24), data = u32(header, 40);
    if (!block || !rate || data < block || data % block || seconds <= 0)
        throw std::runtime_error("Invalid music overlap format");
    uint64_t frame = uint64_t(std::max(0.0, position) * rate);
    if (!loop && frame >= data / block)
        return {};
    uint32_t at = uint32_t(frame % (data / block)) * block;
    uint32_t wanted = uint32_t(seconds * rate) * block;
    Bytes out = header;
    while (wanted) {
        uint32_t n = std::min(wanted, data - at);
        size_t start = out.size();
        out.resize(start + n);
        if (SDL_RWseek(rw.get(), 44 + at, RW_SEEK_SET) < 0 ||
            SDL_RWread(rw.get(), out.data() + start, 1, n) != n)
            throw std::runtime_error("Cannot read music overlap samples");
        wanted -= n;
        at = 0;
        if (!loop)
            break;
    }
    put32(out, 4, uint32_t(out.size() - 8));
    put32(out, 40, uint32_t(out.size() - 44));
    return out;
}
} // namespace tenshi
