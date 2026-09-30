#pragma once
#include "archive.hpp"
#include <cstring>
namespace tenshi {
struct Pixels {
    int w = 0, h = 0, x = 0, y = 0;
    Bytes rgba;
    Pixels() = default;
    Pixels(int width, int height) : w(width), h(height) {
        if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || uint64_t(w) * h * 4 > 128u * 1024 * 1024)
            throw std::runtime_error("Invalid PX dimensions");
        rgba.resize(size_t(w) * h * 4);
    }
    Pixels rows(int start, int count) const {
        if (start < 0 || count < 1 || start + count > h)
            throw std::runtime_error("Invalid image rows");
        Pixels p(w, count);
        std::copy_n(rgba.begin() + size_t(start) * w * 4, p.rgba.size(), p.rgba.begin());
        return p;
    }
};
inline Pixels background(const Archive &a, const std::string &name) {
    auto b = lzss(a.read(name));
    Pixels image(int(u32(b, 0)), int(u32(b, 4)));
    auto tag = u32(b, 16);
    if (tag == 0x80001 && b.size() == 32 + size_t(image.w) * image.h) {
        for (size_t i = 0; i < image.rgba.size(); i += 4) {
            image.rgba[i] = image.rgba[i + 1] = image.rgba[i + 2] = b[32 + i / 4];
            image.rgba[i + 3] = 255;
        }
        return image;
    }
    int kind = a.entry(name).kind;
    if (tag != 0x200001 || b.size() != 32 + image.rgba.size() || kind < 0x80 || kind > 0x89)
        throw std::runtime_error("Unsupported background PX");
    int r = 0, g = 0, blue = 0;
    for (size_t dst = 0; dst < image.rgba.size(); dst += 4) {
        size_t p = 32 + dst;
        int adj = b[p + 3] - (kind & 15);
        blue = (blue + b[p] + adj) & 255;
        g = (g + b[p + 1] + adj) & 255;
        r = (r + b[p + 2] + adj) & 255;
        image.rgba[dst] = r;
        image.rgba[dst + 1] = g;
        image.rgba[dst + 2] = blue;
        image.rgba[dst + 3] = 255;
    }
    return image;
}
// Composite/indexed PX layout follows the MIT-licensed GARbro ImagePX.cs;
// see licenses/GARbro-MIT.txt and the project's original C# decoder.
inline void copyBlock(const Bytes &pixels, int stride, Pixels &dst, int x, int y, int w, int h) {
    for (int row = 0; row < h; row++) {
        int ty = y + row;
        if (ty < 0 || ty >= dst.h)
            continue;
        int left = std::max(0, x), right = std::min(dst.w, x + w);
        if (right <= left)
            continue;
        size_t from = (size_t(row) * stride + left - x) * 4, to = (size_t(ty) * dst.w + left) * 4,
               n = size_t(right - left) * 4;
        requireBytes(pixels, from, n);
        std::copy_n(pixels.begin() + from, n, dst.rgba.begin() + to);
    }
}
inline void colorBlock(const Bytes &b, size_t p, size_t end, Pixels &dst, int x, int y, int w, int h,
                       bool extended) {
    if (w <= 0 || w > 1024 || h <= 0 || h > 8192)
        throw std::runtime_error("Invalid PX block dimensions");
    Bytes pixels(size_t(1024) * h * 4);
    int64_t pos = 0;
    auto get = [&]() {
        if (p > end || end - p < 4)
            throw std::runtime_error("Truncated PX color block");
        int32_t v = int32_t(u32(b, p));
        p += 4;
        return v;
    };
    for (;;) {
        int next = get();
        if (next == -1)
            break;
        if (next < 0 || next > 0xffffff)
            continue;
        pos += next / 4;
        get();
        int count = get(), bytes = extended ? 6 : 4;
        if (count < 0 || uint64_t(count) * bytes > end - p)
            throw std::runtime_error("Invalid PX color run");
        for (int i = 0; i < count; i++, pos++) {
            uint32_t color = u32(b, p);
            p += bytes;
            if (pos < 0 || pos >= int64_t(1024) * h)
                continue;
            size_t at = size_t(pos) * 4;
            pixels[at] = uint8_t(color >> 16);
            pixels[at + 1] = uint8_t(color >> 8);
            pixels[at + 2] = uint8_t(color);
            uint8_t alpha = uint8_t(color >> 24);
            pixels[at + 3] = alpha == 0 ? (extended ? 0 : 255) : uint8_t((color >> 23) + 255);
        }
    }
    copyBlock(pixels, 1024, dst, x, y, w, h);
}
inline void indexedBlock(const Bytes &b, size_t p, size_t end, const Bytes &palette, Pixels &dst, int x,
                         int y, int w, int h) {
    if (w <= 0 || w > 1024 || h <= 0 || h > 8192 || palette.empty())
        throw std::runtime_error("Invalid indexed PX block");
    Bytes pixels(size_t(w) * h * 4);
    int64_t pos = 0;
    bool alpha = true;
    for (;;) {
        if (p > end || end - p < 4)
            throw std::runtime_error("Truncated indexed PX block");
        int32_t code = int32_t(u32(b, p));
        p += 4;
        if (code == -1)
            break;
        if (code & 0x180000)
            alpha = !alpha;
        pos += (code & 0x1ff) * 1024LL + (code >> 21);
        int count = (code >> 9) & 0x3ff;
        if (size_t(count) * (alpha ? 2 : 1) > end - p)
            throw std::runtime_error("Truncated indexed PX run");
        for (int i = 0; i < count; i++, pos++) {
            uint8_t a = alpha ? uint8_t((b[p++] << 1) - 1) : 255;
            size_t c = size_t(b[p++]) * 4;
            requireBytes(palette, c, 4);
            if (pos < 0 || pos >= int64_t(h) * 1024 || pos % 1024 >= w)
                continue;
            size_t at = size_t(pos / 1024 * w + pos % 1024) * 4;
            pixels[at] = palette[c + 2];
            pixels[at + 1] = palette[c + 1];
            pixels[at + 2] = palette[c];
            pixels[at + 3] = a;
        }
    }
    copyBlock(pixels, w, dst, x, y, w, h);
}
inline Pixels pxImage(const Bytes &b, size_t start, size_t end) {
    if (end > b.size() || start > end || end - start < 32)
        throw std::runtime_error("Invalid PX image bounds");
    uint32_t format = u32(b, start + 16);
    bool composite = (format & 0xffff) == 0x40 || (format & 0xffff) == 0x44;
    Pixels image(int(u32(b, start + (composite ? 20 : 0))), int(u32(b, start + (composite ? 24 : 4))));
    image.x = int32_t(u32(b, start + 8));
    image.y = int32_t(u32(b, start + 12));
    if (composite) {
        uint32_t count = u32(b, start);
        size_t base = start + 32 + size_t(count) * 4;
        if (!count || count > 1024 || base > end)
            throw std::runtime_error("Invalid composite PX table");
        Bytes palette;
        for (uint32_t i = 0; i < count; i++) {
            size_t p = base + u32(b, start + 32 + i * 4),
                   limit = i + 1 == count ? end : base + u32(b, start + 36 + i * 4);
            if (p < base || limit > end || p > limit || limit - p < 32)
                throw std::runtime_error("Invalid composite PX bounds");
            int w = int32_t(u32(b, p)), h = int32_t(u32(b, p + 4)), x = int32_t(u32(b, p + 8)),
                y = int32_t(u32(b, p + 12)), type = u16(b, p + 16), bits = u16(b, p + 18);
            if (type == 0 && bits == 32) {
                if (w <= 0 || w > 256 || size_t(w) * 4 > limit - p - 32)
                    throw std::runtime_error("Invalid PX palette");
                palette.assign(b.begin() + p + 32, b.begin() + p + 32 + w * 4);
            } else if (type == 4 && bits == 9)
                indexedBlock(b, p + 32, limit, palette, image, x, y, w, h);
            else if (type == 4 && (bits == 32 || bits == 48))
                colorBlock(b, p + 32, limit, image, x, y, w, h, bits == 48);
            else
                throw std::runtime_error("Unsupported composite PX block");
        }
        return image;
    }
    if (format == 0x200004 || format == 0x300004)
        colorBlock(b, start + 32, end, image, 0, 0, image.w, image.h, format == 0x300004);
    else if ((format == 0x80007 && end - start == 32 + 1024 + size_t(image.w) * image.h) ||
             (format == 0x200001 && end - start == 32 + image.rgba.size())) {
        for (size_t at = 0; at < image.rgba.size(); at += 4) {
            size_t src =
                format == 0x80007 ? start + 32 + size_t(b[start + 32 + 1024 + at / 4]) * 4 : start + 32 + at;
            image.rgba[at] = b[src + 2];
            image.rgba[at + 1] = b[src + 1];
            image.rgba[at + 2] = b[src];
            image.rgba[at + 3] =
                b[src + 3] == 0 ? 255 : uint8_t(((b[src + 3] << 1) | (b[src + 2] >> 7)) + 255);
        }
    } else
        throw std::runtime_error("Unsupported PX layout");
    return image;
}
inline Pixels pxStandalone(const Bytes &packed) {
    auto b = lzss(packed);
    return pxImage(b, 0, b.size());
}
inline Pixels pxFrame(const Bytes &b, int frame) {
    requireBytes(b, 0, 40);
    int count = int(u32(b, 0));
    if (u32(b, 16) != 0x80 || std::memcmp(b.data() + 20, "LeafAquaPlus", 12) || count <= 0 || count > 1024 ||
        frame < 0 || frame >= count)
        throw std::runtime_error("Invalid UI PX container");
    size_t base = 36 + size_t(count - 1) * 4;
    requireBytes(b, 0, base);
    size_t start = base + (frame ? u32(b, 36 + (frame - 1) * 4) : 0),
           end = frame + 1 == count ? b.size() : base + u32(b, 36 + frame * 4);
    if (start < base)
        throw std::runtime_error("Invalid PX frame offset");
    return pxImage(b, start, end);
}
inline Bytes repairedVoice(const Bytes &b) {
    Bytes out;
    out.reserve(b.size() + 32);
    size_t p = 0;
    int headers = 0;
    while (p < b.size()) {
        requireBytes(b, p, 27);
        int segments = b[p + 26];
        size_t table = p + 27, payload = table + segments;
        requireBytes(b, table, segments);
        if (!segments)
            throw std::runtime_error("Empty Ogg segment table");
        size_t size = 0;
        for (int i = 0; i < segments; i++)
            size += b[table + i];
        requireBytes(b, payload, size);
        Bytes page(b.begin() + p, b.begin() + payload);
        std::memcpy(page.data(), "OggS", 4);
        size_t cursor = payload;
        bool packet = true, repair = false;
        for (int i = 0; i < segments; i++) {
            int len = b[table + i];
            if (packet && headers < 3) {
                int type = headers * 2 + 1;
                if (len < 2 || b[cursor] != type || b[cursor + 1] != 1)
                    throw std::runtime_error("Invalid original Vorbis header");
                page.push_back(uint8_t(type));
                for (char c : std::string("vorbis"))
                    page.push_back(c);
                page.insert(page.end(), b.begin() + cursor + 2, b.begin() + cursor + len);
                repair = true;
            } else
                page.insert(page.end(), b.begin() + cursor, b.begin() + cursor + len);
            cursor += len;
            packet = len < 255;
            if (packet && repair) {
                if (page[27 + i] > 250)
                    throw std::runtime_error("Invalid Vorbis lacing");
                page[27 + i] += 5;
                headers++;
                repair = false;
            }
        }
        put32(page, 22, 0);
        uint32_t crc = 0;
        for (auto v : page) {
            crc ^= uint32_t(v) << 24;
            for (int bit = 0; bit < 8; bit++)
                crc = crc & 0x80000000 ? (crc << 1) ^ 0x04c11db7 : crc << 1;
        }
        put32(page, 22, crc);
        out.insert(out.end(), page.begin(), page.end());
        p = payload + size;
    }
    if (headers != 3)
        throw std::runtime_error("Missing Vorbis headers");
    return out;
}
inline Bytes waveHeader(const Bytes &header, uint32_t entrySize) {
    requireBytes(header, 0, 18);
    int ch = header[0], align = header[1], rate = u16(header, 2), bits = u16(header, 4);
    uint32_t bytes = u32(header, 6), length = u32(header, 10);
    if ((ch != 1 && ch != 2) || (bits != 8 && bits != 16) || !rate || align != ch * bits / 8 ||
        bytes != uint32_t(rate * align) || entrySize < 18 || length > entrySize - 18)
        throw std::runtime_error("Invalid original W audio");
    Bytes out(44);
    std::memcpy(out.data(), "RIFF", 4);
    put32(out, 4, length + 36);
    std::memcpy(out.data() + 8, "WAVEfmt ", 8);
    put32(out, 16, 16);
    out[20] = 1;
    out[22] = uint8_t(ch);
    put32(out, 24, rate);
    put32(out, 28, bytes);
    out[32] = uint8_t(align);
    out[34] = uint8_t(bits);
    std::memcpy(out.data() + 36, "data", 4);
    put32(out, 40, length);
    return out;
}
inline Bytes wave(const Bytes &b) {
    auto out = waveHeader(b, uint32_t(b.size()));
    auto n = u32(out, 40);
    out.insert(out.end(), b.begin() + 18, b.begin() + 18 + n);
    return out;
}
} // namespace tenshi
