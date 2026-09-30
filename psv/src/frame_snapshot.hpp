#pragma once
#include "media.hpp"
#include <SDL.h>
#include <memory>
#include <vector>
namespace tenshi {
// Retain immutable draw sources, then rasterize only when an effect needs a snapshot.
// Vita GXM cannot SDL_RenderReadPixels() from SDL_TEXTUREACCESS_TARGET textures.
class FrameSnapshot {
    struct Draw {
        std::shared_ptr<const Pixels> source;
        // A finished nested frame (e.g. a save preview) rasterized only if this frame is rendered.
        std::shared_ptr<const FrameSnapshot> nested;
        SDL_Rect destination, clip;
        SDL_Color modulation;
        SDL_BlendMode blend;
    };
    std::vector<Draw> draws;
    int width, height;
    SDL_Color clearColor;
    mutable std::shared_ptr<const Pixels> rendered;

  public:
    FrameSnapshot(int w = 800, int h = 600, SDL_Color clear = {8, 12, 20, 255})
        : width(w), height(h), clearColor(clear) {}
    void texture(std::shared_ptr<const Pixels> source, SDL_Rect destination, SDL_Rect clip,
                 SDL_Color modulation, SDL_BlendMode blend) {
        if (!source)
            throw std::runtime_error("Snapshot source pixels unavailable");
        draws.push_back({std::move(source), {}, destination, clip, modulation, blend});
    }
    void nestedTexture(std::shared_ptr<const FrameSnapshot> source, SDL_Rect destination, SDL_Rect clip,
                       SDL_Color modulation, SDL_BlendMode blend) {
        if (!source)
            throw std::runtime_error("Snapshot source pixels unavailable");
        draws.push_back({{}, std::move(source), destination, clip, modulation, blend});
    }
    void fill(SDL_Rect destination, SDL_Rect clip, SDL_Color color,
              SDL_BlendMode blend = SDL_BLENDMODE_BLEND) {
        draws.push_back({{}, {}, destination, clip, color, blend});
    }
    // Rendered once and kept; only used for immutable, finished nested frames.
    const Pixels &cached() const {
        if (!rendered)
            rendered = std::make_shared<const Pixels>(render());
        return *rendered;
    }
    // Same arithmetic as a straightforward per-pixel loop, but source coordinates are computed once
    // per row/column (the Vita CPU has no hardware divide) and the alpha 0 / 255 cases, which the
    // blend formula maps to "keep" / "replace", skip the arithmetic.
    Pixels render() const {
        Pixels out(width, height);
        for (size_t i = 0; i < out.rgba.size(); i += 4) {
            out.rgba[i] = clearColor.r;
            out.rgba[i + 1] = clearColor.g;
            out.rgba[i + 2] = clearColor.b;
            out.rgba[i + 3] = clearColor.a;
        }
        std::vector<int> columns;
        for (const auto &d : draws) {
            auto box = d.destination;
            if (box.w <= 0 || box.h <= 0)
                continue;
            int left = std::max({0, box.x, d.clip.x}), top = std::max({0, box.y, d.clip.y});
            int right = std::min({width, box.x + box.w, d.clip.x + d.clip.w}),
                bottom = std::min({height, box.y + box.h, d.clip.y + d.clip.h});
            if (d.blend != SDL_BLENDMODE_BLEND && d.blend != SDL_BLENDMODE_NONE &&
                d.blend != SDL_BLENDMODE_ADD && d.blend != SDL_BLENDMODE_MUL)
                throw std::runtime_error("Unsupported snapshot blend mode");
            if (right <= left || bottom <= top)
                continue;
            const Pixels *image = d.source ? d.source.get() : d.nested ? &d.nested->cached() : nullptr;
            const unsigned mr = d.modulation.r, mg = d.modulation.g, mb = d.modulation.b, ma = d.modulation.a;
            const bool plain = mr == 255 && mg == 255 && mb == 255 && ma == 255;
            const bool blend = d.blend == SDL_BLENDMODE_BLEND;
            if (image) {
                columns.resize(size_t(right - left));
                for (int x = left; x < right; x++)
                    columns[size_t(x - left)] = ((x - box.x) * image->w / box.w) * 4;
            }
            for (int y = top; y < bottom; y++) {
                uint8_t *dst = &out.rgba[(size_t(y) * width + left) * 4];
                const uint8_t *row =
                    image ? &image->rgba[size_t((y - box.y) * image->h / box.h) * image->w * 4] : nullptr;
                for (int x = left; x < right; x++, dst += 4) {
                    unsigned red = mr, green = mg, blue = mb, alpha = ma;
                    if (row) {
                        const uint8_t *s = row + columns[size_t(x - left)];
                        if (plain) {
                            red = s[0];
                            green = s[1];
                            blue = s[2];
                            alpha = s[3];
                        } else {
                            red = red * s[0] / 255;
                            green = green * s[1] / 255;
                            blue = blue * s[2] / 255;
                            alpha = alpha * s[3] / 255;
                        }
                    }
                    if (d.blend == SDL_BLENDMODE_ADD || d.blend == SDL_BLENDMODE_MUL) {
                        unsigned source[3] = {red, green, blue};
                        for (int c = 0; c < 3; ++c) {
                            unsigned value = d.blend == SDL_BLENDMODE_ADD
                                                 ? dst[c] + source[c] * alpha / 255
                                                 : (source[c] * dst[c] + dst[c] * (255 - alpha)) / 255;
                            dst[c] = uint8_t(std::min(255u, value));
                        }
                    } else if (!blend || alpha == 255) {
                        dst[0] = uint8_t(red);
                        dst[1] = uint8_t(green);
                        dst[2] = uint8_t(blue);
                        dst[3] = uint8_t(alpha);
                    } else if (alpha) {
                        unsigned inverse = 255 - alpha;
                        dst[0] = uint8_t((red * alpha + dst[0] * inverse) / 255);
                        dst[1] = uint8_t((green * alpha + dst[1] * inverse) / 255);
                        dst[2] = uint8_t((blue * alpha + dst[2] * inverse) / 255);
                        dst[3] = uint8_t(alpha + dst[3] * inverse / 255);
                    }
                }
            }
        }
        return out;
    }
};
class SnapshotRecorder {
    struct Session {
        SDL_Texture *target;
        FrameSnapshot frame;
    };
    std::vector<Session> sessions;
    SDL_Rect clip(SDL_Renderer *r) const {
        SDL_Rect result{0, 0, 800, 600};
        if (SDL_RenderIsClipEnabled(r))
            SDL_RenderGetClipRect(r, &result);
        return result;
    }
    bool recording(SDL_Renderer *r) const {
        return !sessions.empty() && sessions.back().target == SDL_GetRenderTarget(r);
    }

  public:
    FrameSnapshot last;
    void begin(SDL_Texture *target) {
        sessions.push_back({target, {}});
    }
    void finish() {
        if (sessions.empty())
            throw std::runtime_error("No snapshot frame");
        last = std::move(sessions.back().frame);
        sessions.pop_back();
    }
    Pixels finishNested() {
        return finishNestedFrame()->render();
    }
    // The nested frame is kept as a draw list and rasterized only if a parent frame is rendered.
    std::shared_ptr<const FrameSnapshot> finishNestedFrame() {
        if (sessions.size() < 2)
            throw std::runtime_error("No nested snapshot");
        auto frame = std::make_shared<const FrameSnapshot>(std::move(sessions.back().frame));
        sessions.pop_back();
        return frame;
    }
    Pixels current() const {
        return sessions.empty() ? last.render() : sessions.back().frame.render();
    }
    void discard() {
        sessions.clear();
    }
    void texture(SDL_Renderer *r, SDL_Texture *texture, std::shared_ptr<const Pixels> source,
                 SDL_Rect destination, std::shared_ptr<const FrameSnapshot> nested = {}) {
        if (!recording(r))
            return;
        SDL_Color mod{255, 255, 255, 255};
        SDL_GetTextureColorMod(texture, &mod.r, &mod.g, &mod.b);
        SDL_GetTextureAlphaMod(texture, &mod.a);
        SDL_BlendMode blend;
        SDL_GetTextureBlendMode(texture, &blend);
        if (nested && !source)
            sessions.back().frame.nestedTexture(std::move(nested), destination, clip(r), mod, blend);
        else
            sessions.back().frame.texture(std::move(source), destination, clip(r), mod, blend);
    }
    void fill(SDL_Renderer *r, SDL_Rect destination, SDL_Color color) {
        if (recording(r))
            sessions.back().frame.fill(destination, clip(r), color);
    }
    void outline(SDL_Renderer *r, SDL_Rect q, SDL_Color color) {
        fill(r, {q.x, q.y, q.w, 1}, color);
        fill(r, {q.x, q.y + q.h - 1, q.w, 1}, color);
        if (q.h > 2) {
            fill(r, {q.x, q.y + 1, 1, q.h - 2}, color);
            fill(r, {q.x + q.w - 1, q.y + 1, 1, q.h - 2}, color);
        }
    }
};
} // namespace tenshi
