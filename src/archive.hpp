#pragma once
#include "gb18030_table.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace tenshi {
using Bytes = std::vector<uint8_t>;
inline void requireBytes(const Bytes &b, size_t at, size_t n) {
    if (at > b.size() || n > b.size() - at)
        throw std::runtime_error("Truncated original resource");
}
inline uint16_t u16(const Bytes &b, size_t p) {
    requireBytes(b, p, 2);
    return b[p] | (uint16_t(b[p + 1]) << 8);
}
inline uint32_t u32(const Bytes &b, size_t p) {
    requireBytes(b, p, 4);
    return u16(b, p) | (uint32_t(u16(b, p + 2)) << 16);
}
inline void put32(Bytes &b, size_t p, uint32_t v) {
    requireBytes(b, p, 4);
    for (int i = 0; i < 4; i++)
        b[p + i] = uint8_t(v >> (i * 8));
}
inline std::string lower(std::string s) {
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    return s;
}
class Archive {
  public:
    struct Entry {
        uint64_t offset;
        uint32_t size;
        uint8_t kind;
    };
    std::string path;
    std::map<std::string, Entry> entries;
    explicit Archive(std::string p) : path(std::move(p)) {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            throw std::runtime_error("Missing original archive: " + path);
        f.seekg(0, std::ios::end);
        auto end = f.tellg();
        if (end < 4)
            throw std::runtime_error("Invalid archive: " + path);
        f.seekg(0);
        Bytes head(4);
        f.read(reinterpret_cast<char *>(head.data()), 4);
        if (u16(head, 0) != 0xaf1e)
            throw std::runtime_error("Invalid archive signature: " + path);
        uint32_t count = u16(head, 2);
        uint64_t base = 4 + uint64_t(count) * 32;
        if (base > uint64_t(end))
            throw std::runtime_error("Truncated archive index");
        Bytes row(32);
        for (uint32_t i = 0; i < count; i++) {
            f.read(reinterpret_cast<char *>(row.data()), 32);
            if (!f)
                throw std::runtime_error("Truncated archive index");
            size_t len = 0;
            while (len < 23 && row[len])
                len++;
            auto name = lower(std::string(reinterpret_cast<char *>(row.data()), len));
            Entry e{base + u32(row, 28), u32(row, 24), row[23]};
            if (e.offset > uint64_t(end) || e.size > uint64_t(end) - e.offset)
                throw std::runtime_error("Invalid archive entry: " + name);
            auto old = entries.find(name);
            if (old == entries.end() || (old->second.kind < 0x80 && e.kind >= 0x80))
                entries[name] = e;
        }
    }
    bool contains(const std::string &name) const {
        return entries.count(lower(name)) != 0;
    }
    const Entry &entry(const std::string &name) const {
        auto it = entries.find(lower(name));
        if (it == entries.end())
            throw std::runtime_error("Missing resource: " + name + " in " + path);
        return it->second;
    }
    Bytes read(const std::string &name) const {
        auto e = entry(name);
        if (e.size > 128u * 1024 * 1024)
            throw std::runtime_error("Resource exceeds memory limit: " + name);
        std::ifstream f(path, std::ios::binary);
        f.seekg(std::streamoff(e.offset));
        Bytes b(e.size);
        if (!f.read(reinterpret_cast<char *>(b.data()), b.size()))
            throw std::runtime_error("Cannot read resource: " + name);
        return b;
    }
};
// 4 KiB-window LZSS with the window starting at 0xfee and zero-filled. Output byte n occupies window
// slot (0xfee + n) & 4095, so a reference to slot `at` is a copy from distance
// ((0xfee + n - at) & 4095) or 4096, and anything before the output start reads as 0. Copying from
// the output directly (instead of a separate ring) and checking bounds once per 8-item group keeps
// the result identical while running several times faster on the Vita CPU.
inline Bytes lzss(const Bytes &src) {
    auto size = u32(src, 0);
    if (!size || size > 128u * 1024 * 1024)
        throw std::runtime_error("Invalid LZSS size");
    Bytes out(size);
    const uint8_t *in = src.data();
    uint8_t *o = out.data();
    const size_t inSize = src.size(), total = size;
    size_t p = 4, n = 0;
    auto copy = [&](size_t at, size_t len) {
        size_t distance = (0xfee + n - at) & 4095;
        if (!distance)
            distance = 4096;
        len = std::min(len, total - n);
        for (size_t i = 0; i < len; i++, n++)
            o[n] = n >= distance ? o[n - distance] : 0;
    };
    while (n < total) {
        requireBytes(src, p, 1);
        unsigned flags = in[p++];
        if (inSize - p >= 16 && total - n >= 8 * 18) {
            // A whole group fits: at most 16 input bytes, at most 8 x 18 output bytes.
            for (int bit = 0; bit < 8; bit++) {
                if (flags & (1u << bit))
                    o[n++] = in[p++];
                else {
                    unsigned lo = in[p], hi = in[p + 1];
                    p += 2;
                    copy(lo | ((hi & 0xf0u) << 4), (hi & 15u) + 3);
                }
            }
            continue;
        }
        for (int bit = 0; bit < 8 && n < total; bit++) {
            if (flags & (1u << bit)) {
                requireBytes(src, p, 1);
                o[n++] = in[p++];
            } else {
                requireBytes(src, p, 2);
                unsigned lo = in[p], hi = in[p + 1];
                p += 2;
                copy(lo | ((hi & 0xf0u) << 4), (hi & 15u) + 3);
            }
        }
    }
    return out;
}
// Byte count of the unformatted event, matching Godot's GB18030 auto-wait calculation.
inline int gb18030ByteCount(const std::string &text) {
    static const auto pairs = [] {
        std::array<bool, 65536> out{};
        for (auto c : gbPairs)
            if (c < out.size())
                out[c] = true;
        return out;
    }();
    int bytes = 0;
    for (size_t i = 0; i < text.size();) {
        uint32_t c = uint8_t(text[i++]);
        if (c < 128) {
            ++bytes;
            continue;
        }
        int count = c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
        c &= (1u << (6 - count)) - 1;
        while (count-- > 0) {
            if (i >= text.size() || (uint8_t(text[i]) & 0xc0) != 0x80)
                throw std::runtime_error("Invalid UTF-8 message");
            c = (c << 6) | (uint8_t(text[i++]) & 63);
        }
        bytes += c < pairs.size() && pairs[c] ? 2 : 4;
    }
    return bytes;
}
inline std::string gb18030(const uint8_t *data, size_t size) {
    std::string out;
    auto append = [&](uint32_t c) {
        if (c < 0x80)
            out.push_back(char(c));
        else if (c < 0x800) {
            out.push_back(char(0xc0 | (c >> 6)));
            out.push_back(char(0x80 | (c & 63)));
        } else if (c < 0x10000) {
            out.push_back(char(0xe0 | (c >> 12)));
            out.push_back(char(0x80 | ((c >> 6) & 63)));
            out.push_back(char(0x80 | (c & 63)));
        } else {
            out.push_back(char(0xf0 | (c >> 18)));
            out.push_back(char(0x80 | ((c >> 12) & 63)));
            out.push_back(char(0x80 | ((c >> 6) & 63)));
            out.push_back(char(0x80 | (c & 63)));
        }
    };
    for (size_t p = 0; p < size;) {
        uint8_t a = data[p++];
        if (a < 0x80) {
            append(a);
            continue;
        }
        if (a < 0x81 || a > 0xfe || p == size)
            throw std::runtime_error("Invalid GB18030 text");
        uint8_t b = data[p++];
        if (b >= 0x40 && b <= 0xfe && b != 0x7f) {
            append(gbPairs[(a - 0x81) * 190 + b - 0x40 - (b > 0x7f ? 1 : 0)]);
            continue;
        }
        if (b < 0x30 || b > 0x39 || size - p < 2)
            throw std::runtime_error("Invalid GB18030 sequence");
        uint8_t c = data[p++], d = data[p++];
        if (c < 0x81 || c > 0xfe || d < 0x30 || d > 0x39)
            throw std::runtime_error("Invalid GB18030 sequence");
        uint32_t point = (((a - 0x81) * 10 + b - 0x30) * 126 + c - 0x81) * 10 + d - 0x30;
        bool found = false;
        for (auto range : gbRanges)
            if (point >= range.first && point <= range.last) {
                append(range.unicode + point - range.first);
                found = true;
                break;
            }
        if (!found)
            throw std::runtime_error("Unassigned GB18030 code point");
    }
    return out;
}
} // namespace tenshi
