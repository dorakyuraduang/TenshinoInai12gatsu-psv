#pragma once
#include "media.hpp"
#include <SDL.h>
#include <array>
#include <functional>
#include <limits>
#include <memory>

namespace tenshi {
// WAV decoding calls RWops from SDL's audio thread. This stream serves only
// memory there; archive I/O happens during open or on the dedicated worker.
struct PrefetchedArchiveWave {
    static constexpr size_t PageBytes = 1024 * 1024;
    struct Options {
        size_t pageBytes = PageBytes;
        // Test seam for slow storage; called with no cache lock held.
        std::function<void()> beforeRead;
    };
    struct Stats {
        uint64_t underrunBytes = 0, ioErrors = 0;
        size_t bufferBytes = 0;
    };
    struct Page {
        Bytes bytes;
        uint64_t index = std::numeric_limits<uint64_t>::max();
    };
    std::ifstream file;
    Bytes header, staging;
    // Page zero is permanent; two rolling pages plus staging bound PCM
    // allocations to four 1 MiB buffers, including during a blocked disk read.
    std::array<Page, 3> pages;
    Options options;
    Sint64 base = 0, pos = 0, length = 0;
    uint64_t pcmLength = 0, pageCount = 0, wanted = 0, underruns = 0, errors = 0;
    uint8_t silence = 0;
    SDL_mutex *cacheMutex = nullptr;
    SDL_cond *wake = nullptr;
    SDL_Thread *worker = nullptr;
    bool quit = false;

    ~PrefetchedArchiveWave() {
        if (worker) {
            SDL_LockMutex(cacheMutex);
            quit = true;
            SDL_CondSignal(wake);
            SDL_UnlockMutex(cacheMutex);
            SDL_WaitThread(worker, nullptr);
        }
        if (wake) SDL_DestroyCond(wake);
        if (cacheMutex) SDL_DestroyMutex(cacheMutex);
    }
    static PrefetchedArchiveWave &get(SDL_RWops *rw) {
        return *static_cast<PrefetchedArchiveWave *>(rw->hidden.unknown.data1);
    }
    size_t pageLength(uint64_t index) const {
        return size_t(std::min(uint64_t(options.pageBytes), pcmLength - index * options.pageBytes));
    }
    uint64_t nextPage() const {
        return wanted + 1 < pageCount ? wanted + 1 : 0;
    }
    const Page *find(uint64_t index) const {
        for (const auto &page : pages)
            if (page.index == index) return &page;
        return nullptr;
    }
    // cacheMutex held; page zero stays resident for every loop boundary.
    void requestPosition() {
        if (!pageCount || pos >= length) return;
        uint64_t index = pos <= 44 ? 0 : std::min(uint64_t(pos - 44) / options.pageBytes, pageCount - 1);
        if (index != wanted) {
            wanted = index;
            SDL_CondSignal(wake);
        }
    }
    bool load(uint64_t index, Bytes &out) {
        if (options.beforeRead) options.beforeRead();
        size_t n = pageLength(index);
        file.clear();
        file.seekg(std::streamoff(base + Sint64(index * options.pageBytes)));
        if (!file) return false;
        file.read(reinterpret_cast<char *>(out.data()), std::streamsize(n));
        return size_t(file.gcount()) == n;
    }
    static int run(void *user) {
        auto &s = *static_cast<PrefetchedArchiveWave *>(user);
        SDL_LockMutex(s.cacheMutex);
        while (!s.quit) {
            uint64_t index = s.wanted;
            if (!s.pageCount || (s.find(index) && s.find(index = s.nextPage()))) {
                SDL_CondWait(s.wake, s.cacheMutex);
                continue;
            }
            SDL_UnlockMutex(s.cacheMutex);
            bool ok = false;
            try { ok = s.load(index, s.staging); } catch (...) {}
            if (!ok) std::fill_n(s.staging.begin(), s.pageLength(index), s.silence);
            SDL_LockMutex(s.cacheMutex);
            if (!ok) ++s.errors;
            if (!s.quit && (index == s.wanted || index == s.nextPage()) && !s.find(index)) {
                // The position may have changed during disk I/O. Do not evict
                // either wanted page or the permanent loop head for an obsolete request.
                for (auto &page : s.pages)
                    if (page.index != 0 && page.index != s.wanted && page.index != s.nextPage()) {
                        page.bytes.swap(s.staging);
                        page.index = index;
                        break;
                    }
            }
        }
        SDL_UnlockMutex(s.cacheMutex);
        return 0;
    }
    static Sint64 size(SDL_RWops *rw) { return get(rw).length; }
    static Sint64 seek(SDL_RWops *rw, Sint64 offset, int whence) {
        auto &s = get(rw);
        SDL_LockMutex(s.cacheMutex);
        Sint64 origin = whence == RW_SEEK_SET ? 0 : whence == RW_SEEK_CUR ? s.pos
                      : whence == RW_SEEK_END ? s.length : -1;
        if (origin < 0 || offset < -origin || offset > s.length - origin) {
            SDL_UnlockMutex(s.cacheMutex);
            return SDL_SetError("Seek outside prefetched audio entry"), -1;
        }
        s.pos = origin + offset;
        s.requestPosition();
        Sint64 result = s.pos;
        SDL_UnlockMutex(s.cacheMutex);
        return result;
    }
    static size_t read(SDL_RWops *rw, void *dst, size_t unit, size_t count) {
        auto &s = get(rw);
        if (!unit || !count) return 0;
        SDL_LockMutex(s.cacheMutex);
        size_t n = std::min(size_t(s.length - s.pos) / unit, count), left = n * unit;
        auto *out = static_cast<uint8_t *>(dst);
        if (s.pos < 44) {
            size_t take = std::min(left, size_t(44 - s.pos));
            std::memcpy(out, s.header.data() + s.pos, take);
            s.pos += take; out += take; left -= take;
        }
        while (left) {
            uint64_t pcmPos = uint64_t(s.pos - 44), index = pcmPos / s.options.pageBytes;
            size_t at = size_t(pcmPos % s.options.pageBytes);
            size_t take = std::min(left, s.pageLength(index) - at);
            if (const Page *page = s.find(index)) std::memcpy(out, page->bytes.data() + at, take);
            else {
                // Returning 0 on a temporary miss would masquerade as EOF.
                // Advance with format-correct silence and record the miss.
                std::memset(out, s.silence, take);
                s.underruns += take;
            }
            s.pos += Sint64(take); out += take; left -= take;
            s.requestPosition();
        }
        SDL_UnlockMutex(s.cacheMutex);
        return n;
    }
    static size_t write(SDL_RWops *, const void *, size_t, size_t) { return 0; }
    static int close(SDL_RWops *rw) {
        delete &get(rw);
        SDL_FreeRW(rw);
        return 0;
    }
    Stats stats() {
        SDL_LockMutex(cacheMutex);
        Stats out{underruns, errors, staging.capacity() + pages[0].bytes.capacity() + pages[1].bytes.capacity() + pages[2].bytes.capacity()};
        SDL_UnlockMutex(cacheMutex);
        return out;
    }
    // Nonblocking query for explicit seeks prepared off the audio thread.
    bool cached(Sint64 offset, size_t bytes) {
        if (offset < 0 || offset > length || uint64_t(bytes) > uint64_t(length - offset)) return false;
        SDL_LockMutex(cacheMutex);
        bool ready = true;
        uint64_t end = uint64_t(offset) + bytes, at = uint64_t(std::max(Sint64(44), offset));
        while (at < end) {
            uint64_t index = (at - 44) / options.pageBytes;
            if (!find(index)) { ready = false; break; }
            at = 44 + (index + 1) * options.pageBytes;
        }
        SDL_UnlockMutex(cacheMutex);
        return ready;
    }
    static SDL_RWops *open(const Archive &archive, const std::string &name) {
        return open(archive, name, Options{});
    }
    static SDL_RWops *open(const Archive &archive, const std::string &name, Options options) {
        auto e = archive.entry(name);
        auto s = std::make_unique<PrefetchedArchiveWave>();
        s->options = std::move(options);
        s->file.open(archive.path, std::ios::binary);
        if (!s->file || e.size < 18) throw std::runtime_error("Cannot open prefetched audio entry: " + name);
        Bytes head(18);
        s->file.seekg(std::streamoff(e.offset));
        if (!s->file.read(reinterpret_cast<char *>(head.data()), 18))
            throw std::runtime_error("Cannot read prefetched audio header");
        s->header = waveHeader(head, e.size);
        if (!s->options.pageBytes || s->options.pageBytes > PageBytes || s->options.pageBytes % u16(s->header, 32))
            throw std::runtime_error("Invalid audio prefetch page size");
        s->silence = u16(s->header, 34) == 8 ? 128 : 0;
        s->base = Sint64(e.offset) + 18;
        s->pcmLength = u32(s->header, 40);
        s->length = 44 + Sint64(s->pcmLength);
        s->pageCount = (s->pcmLength + s->options.pageBytes - 1) / s->options.pageBytes;
        s->cacheMutex = SDL_CreateMutex();
        s->wake = SDL_CreateCond();
        if (!s->cacheMutex || !s->wake) throw std::runtime_error("Cannot allocate audio prefetch synchronization");
        for (auto &page : s->pages) page.bytes.resize(s->options.pageBytes);
        s->staging.resize(s->options.pageBytes);
        for (uint64_t index = 0; index < std::min(uint64_t(2), s->pageCount); ++index) {
            if (!s->load(index, s->pages[index].bytes)) throw std::runtime_error("Cannot prefill audio entry: " + name);
            s->pages[index].index = index;
        }
        s->worker = SDL_CreateThreadWithStackSize(run, "music-prefetch", 64 * 1024, s.get());
        if (!s->worker) throw std::runtime_error(std::string("Cannot start audio prefetch: ") + SDL_GetError());
        auto *rw = SDL_AllocRW();
        if (!rw) throw std::runtime_error("Cannot allocate prefetched audio stream");
        rw->size = size; rw->seek = seek; rw->read = read; rw->write = write; rw->close = close;
        rw->type = SDL_RWOPS_UNKNOWN;
        rw->hidden.unknown.data1 = s.release(); rw->hidden.unknown.data2 = nullptr;
        return rw;
    }
};
} // namespace tenshi
