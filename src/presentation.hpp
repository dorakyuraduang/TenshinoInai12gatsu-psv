#pragma once
#include "media.hpp"
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
namespace tenshi {
struct Glyph {
    std::string text;
    float x = 0, y = 0, advance = 0, offset = 0;
    int size = 24;
};
struct NativeMessageLayout {
    std::vector<std::vector<Glyph>> pages;
    bool full = false;
    float length(int page) const {
        auto &p = pages.at(page);
        return p.empty() ? 0 : p.back().offset + p.back().advance;
    }
    void set(const std::string &text, bool fullPage) {
        full = fullPage;
        pages = {{}};
        std::vector<Glyph> line;
        std::vector<int> styles;
        float x = full ? 0 : 2, offset = 0, height = 0;
        int row = 0, size = full ? 26 : 24, leading = full ? 16 : 4, compression = 0;
        auto newline = [&]() {
            int maximum = line.empty() ? size : 0;
            for (auto &g : line)
                maximum = std::max(maximum, g.size);
            float lineHeight = maximum + leading;
            if (row >= (full ? 12 : 3) || height + lineHeight > (full ? 520 : 90)) {
                row = 0;
                height = offset = 0;
                pages.emplace_back();
            }
            height += lineHeight;
            int shift = compression;
            for (auto g : line) {
                g.x = std::max(full ? 0.f : 2.f, g.x + shift);
                g.y = height;
                g.offset = offset;
                pages.back().push_back(g);
                offset += g.advance;
                shift += compression;
            }
            line.clear();
            compression = 0;
            row++;
            x = full ? 0 : 2;
        };
        for (size_t i = 0; i < text.size();) {
            if (text.compare(i, 2, "$p") == 0) {
                size_t p = i + 2, end = p;
                if (end < text.size() && text[end] == '-')
                    end++;
                while (end < text.size() && text[end] >= '0' && text[end] <= '9')
                    end++;
                if (end > p && !(end == p + 1 && text[p] == '-')) {
                    long requested = std::strtol(text.substr(p, end - p).c_str(), nullptr, 10);
                    size = int(std::clamp(requested, 1L, long((full ? 520 : 90) - leading)));
                    i = end;
                    continue;
                }
            }
            if (text[i] == '<') {
                styles.push_back(size);
                i++;
                continue;
            }
            if (text[i] == '>') {
                if (!styles.empty()) {
                    size = styles.back();
                    styles.pop_back();
                }
                i++;
                continue;
            }
            unsigned char c = text[i];
            size_t n = c < 128 ? 1 : c < 224 ? 2 : c < 240 ? 3 : 4;
            if (i + n > text.size())
                throw std::runtime_error("Truncated UTF-8 message");
            auto value = text.substr(i, n);
            i += n;
            if (value == "\r")
                continue;
            if (value == "\n" || value == "\x7f") {
                newline();
                continue;
            }
            uint32_t scalar = c;
            if (n > 1) {
                scalar = c & ((1u << (7 - n)) - 1);
                for (size_t j = 1; j < n; j++)
                    scalar = (scalar << 6) | (uint8_t(value[j]) & 63);
            }
            float advance = scalar < 256 ? size >> 1 : size;
            static const std::string closing = "ぁぃぅぇぉっゃゅょんをァィゥェォッャュョンヲ、。，．・：；？"
                                               "！゛゜ーヽヾ々’”）〕］｝〉》」』】";
            static const std::string opening = "‘“（〔［｛〈《「『【";
            if (x + advance >= (full ? 640 : 480)) {
                if (!line.empty() && opening.find(line.back().text) != std::string::npos) {
                    auto g = line.back();
                    line.pop_back();
                    compression = 1;
                    newline();
                    g.x = x;
                    line.push_back(g);
                    x += g.advance;
                } else if (closing.find(value) != std::string::npos && compression >= -1)
                    compression--;
                else
                    newline();
            }
            line.push_back({value, x, 0, advance, 0, size});
            x += advance;
        }
        if (!line.empty())
            newline();
        if (pages.size() > 1 && pages.back().empty())
            pages.pop_back();
    }
};
struct EffectSample {
    int x = 0, y = 0;
    float zoom = 1, flash = 0;
};
inline int originalSin(int angle) {
    static const short half[] = {
        0,   6,   12,  18,  25,  31,  37,  44,  50,  56,  62,  68,  74,  80,  86,  92,  98,  104, 110,
        115, 121, 127, 132, 137, 143, 148, 153, 158, 163, 168, 173, 177, 182, 186, 190, 195, 199, 203,
        206, 210, 214, 217, 220, 224, 227, 230, 232, 235, 237, 240, 242, 244, 246, 248, 249, 251, 252,
        253, 254, 255, 255, 256, 256, 256, 256, 256, 256, 256, 255, 254, 253, 252, 251, 250, 248, 247,
        245, 243, 241, 239, 236, 234, 231, 228, 225, 222, 219, 215, 212, 208, 205, 201, 197, 193, 188,
        184, 180, 175, 170, 166, 161, 156, 151, 145, 140, 135, 129, 124, 118, 113, 107, 101, 95,  89,
        83,  77,  71,  65,  59,  53,  47,  40,  34,  28,  22,  15,  9,   3};
    int i = angle & 255;
    return i < 128 ? half[i] : -half[255 - i];
}
inline EffectSample effectSample(int effect, int frame) {
    EffectSample s;
    if (effect == 4959)
        s.x = frame < 32 ? originalSin(frame * 16) / 5 : 0;
    if (effect == 6200)
        s.y = frame < 40 ? originalSin(frame * 20) / ((400 + frame * (frame - 1)) / 100) : 0;
    if (effect == 5251 && frame < 40) {
        int d = (2 + 10 * frame + 2 * frame * (frame - 1)) / 100, n = originalSin(frame * 14 + 64);
        s.x = d ? n / d : (n > 0 ? 32767 : n < 0 ? -32767 : 0);
        s.flash = std::max(0, 128 - 6 * frame) / 128.f;
    }
    if (effect == 6036)
        s.zoom = frame % 5 == 1 ? 4096.f / 4032 : frame % 5 == 2 || frame % 5 == 3 ? 4096.f / 3968 : 1;
    return s;
}
inline const char *wipeName(int id) {
    switch (id) {
    case 30:
    case 34:
        return "wipe01.px";
    case 31:
    case 35:
        return "wipe01R.px";
    case 32:
        return "wipe02R.px";
    case 33:
        return "wipe02.px";
    case 36:
        return "wave00.px";
    case 37:
        return "wave01.px";
    default:
        return nullptr;
    }
}
inline int transitionStep(int id, int step) {
    return wipeName(id) ? (id == 34 || id == 35   ? 8
                           : id == 36 || id == 37 ? 2
                                                  : 4)
           : id == 3    ? 8
           : id == 12   ? 4
           : step > 0   ? step
                        : 2;
}
inline int effectFrames(int id, int step = 0) {
    if (id >= 4959)
        return id == 4959 ? 33 : id == 6036 ? 91 : id == 5251 || id == 6200 ? 41 : 0;
    if (id == 2 || id == 3 || id == 12 || id == 41 || id == 50 || wipeName(id)) {
        int n = wipeName(id) ? 256 : 128, s = transitionStep(id, step);
        return (n + s - 1) / s;
    }
    return 0;
}
inline uint8_t wipeAlpha(int mask, int frame, int step) {
    return uint8_t(std::clamp((mask - std::min(255, frame * step)) * 255 / 8, 0, 255));
}
inline void blit(Pixels &dst, const Pixels &src, int x, int y) {
    for (int row = std::max(0, -y); row < std::min(src.h, dst.h - y); row++)
        for (int col = std::max(0, -x); col < std::min(src.w, dst.w - x); col++) {
            size_t a = (size_t(row) * src.w + col) * 4, b = (size_t(row + y) * dst.w + col + x) * 4;
            unsigned alpha = src.rgba[a + 3];
            // Exact shortcuts of the formula below: alpha 0 keeps dst, alpha 255 yields src.
            if (alpha == 0)
                continue;
            if (alpha == 255) {
                std::memcpy(&dst.rgba[b], &src.rgba[a], 3);
                dst.rgba[b + 3] = 255;
                continue;
            }
            for (int c = 0; c < 3; c++)
                dst.rgba[b + c] = uint8_t(
                    (unsigned(src.rgba[a + c]) * alpha + unsigned(dst.rgba[b + c]) * (255 - alpha) + 127) /
                    255);
            dst.rgba[b + 3] = uint8_t(alpha + (unsigned(dst.rgba[b + 3]) * (255 - alpha) + 127) / 255);
        }
}
inline void sceneTone(Pixels &image, int tone) {
    if (tone != 42 && tone != 43)
        return;
    for (size_t p = 0; p < image.rgba.size(); p += 4) {
        int g = std::min(255, int(image.rgba[p + 1]) + 32);
        if (tone == 43)
            g = 255 - g;
        image.rgba[p] = image.rgba[p + 1] = image.rgba[p + 2] = uint8_t(g);
    }
}
} // namespace tenshi
