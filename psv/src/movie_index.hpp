#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
// Fixed bundled movie: index generated from its MP4 sample table by index-opening.py.
// The runtime never scans/guesses access-unit boundaries, and the MP4 stays byte-identical.
struct MoviePacket {
    uint64_t offset;
    uint32_t size;
    int64_t pts;
};
class MovieIndex {
    using File = std::unique_ptr<FILE, decltype(&fclose)>;
    File media{nullptr, fclose};
    static uint64_t number(FILE *f, int bytes) {
        uint64_t value = 0;
        for (int i = 0; i < bytes; ++i) {
            int c = fgetc(f);
            if (c == EOF)
                throw std::runtime_error("Truncated movie packet index");
            value |= uint64_t(c) << (8 * i);
        }
        return value;
    }

  public:
    uint32_t width = 0, height = 0, refs = 0, rate = 0, channels = 0;
    uint64_t audioSamples = 0, duration90k = 0;
    std::vector<uint8_t> config;
    std::vector<MoviePacket> video, audio;
    void close() {
        media.reset();
        video.clear();
        audio.clear();
        config.clear();
    }
    void open(const std::string &name) {
        close();
        File index(fopen((name.substr(0, name.find_last_of('.')) + ".idx").c_str(), "rb"), fclose);
        media.reset(fopen(name.c_str(), "rb"));
        if (!index || !media)
            throw std::runtime_error("Cannot open bundled MP4/packet index");
        char magic[8];
        if (fread(magic, 1, 8, index.get()) != 8 || memcmp(magic, "TNSMP401", 8))
            throw std::runtime_error("Invalid movie index version");
        auto u32 = [&]() { return uint32_t(number(index.get(), 4)); };
        width = u32();
        height = u32();
        refs = u32();
        rate = u32();
        channels = u32();
        auto nv = u32(), na = u32();
        uint64_t bytes = number(index.get(), 8);
        audioSamples = number(index.get(), 8);
        duration90k = number(index.get(), 8);
        auto nc = u32();
        if (width != 800 || height != 600 || refs != 2 || rate != 44100 || channels != 2 || nv != 2695 ||
            na < 3800 || na > 4000 || nc > 1024 || nc < 8 || duration90k != 2695 * 3000 ||
            audioSamples < 3960000 || audioSamples > 3963000)
            throw std::runtime_error("Unexpected bundled movie index parameters");
        if (fseek(media.get(), 0, SEEK_END) || uint64_t(ftell(media.get())) != bytes)
            throw std::runtime_error("MP4 and packet index do not match");
        config.resize(nc);
        if (fread(config.data(), 1, nc, index.get()) != nc)
            throw std::runtime_error("Truncated AVC parameter sets");
        for (int kind = 0; kind < 2; ++kind) {
            auto &out = kind ? audio : video;
            for (uint32_t i = 0; i < (kind ? na : nv); ++i) {
                MoviePacket p{number(index.get(), 8), u32(), int64_t(number(index.get(), 8))};
                if (!p.size || p.size > (kind ? 1536u : 512u * 1024) || p.offset >= bytes ||
                    p.size > bytes - p.offset ||
                    p.pts != (kind ? (int64_t(i) - 1) * 1024 : int64_t(i) * 3000))
                    throw std::runtime_error("Invalid movie packet bounds/timing");
                out.push_back(p);
            }
        }
        if (fgetc(index.get()) != EOF)
            throw std::runtime_error("Unexpected trailing movie index data");
    }
    void read(const MoviePacket &p, std::vector<uint8_t> &out, bool avc = false, bool first = false) {
        out.resize(p.size);
        if (fseek(media.get(), long(p.offset), SEEK_SET) ||
            fread(out.data(), 1, p.size, media.get()) != p.size)
            throw std::runtime_error("Cannot read movie access unit");
        if (!avc)
            return;
        size_t pos = 0;
        while (pos < out.size()) {
            if (out.size() - pos < 4)
                throw std::runtime_error("Truncated AVC NAL length");
            uint32_t n = (uint32_t(out[pos]) << 24) | (uint32_t(out[pos + 1]) << 16) |
                         (uint32_t(out[pos + 2]) << 8) | out[pos + 3];
            if (!n || n > out.size() - pos - 4)
                throw std::runtime_error("Invalid AVC NAL size");
            out[pos] = out[pos + 1] = out[pos + 2] = 0;
            out[pos + 3] = 1;
            pos += 4 + n;
        }
        if (first)
            out.insert(out.begin(), config.begin(), config.end());
    }
};
