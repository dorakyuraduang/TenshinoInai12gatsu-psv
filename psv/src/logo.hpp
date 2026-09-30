#pragma once
#include "frame_snapshot.hpp"
#include <SDL.h>
#include <array>
#include <cmath>
#include <memory>
namespace tenshi {
// Two standard GXM blend passes implement D*(1-a*k) + S*a*k per colour channel.
// MUL: D * (a*(1-k)) + D*(1-a). ADD: S*k*a + D. No custom shader or frame upload.
class LogoRenderer {
    std::array<SDL_Texture *, 3> textures{};
    std::array<std::shared_ptr<const Pixels>, 3> images;
    void sprite(SDL_Renderer *r, SnapshotRecorder &snapshots, int index, int x, int y, SDL_Color mod) {
        auto *texture = textures[index];
        SDL_SetTextureColorMod(texture, mod.r, mod.g, mod.b);
        SDL_SetTextureAlphaMod(texture, mod.a);
        SDL_Rect dst{x, y, images[index]->w, images[index]->h};
        snapshots.texture(r, texture, images[index], dst);
        if (SDL_RenderCopy(r, texture, nullptr, &dst))
            throw std::runtime_error(SDL_GetError());
    }

  public:
    size_t uploadBytes = 0;
    unsigned draws = 0;
    uint64_t submitTicks = 0, peakTicks = 0;
    ~LogoRenderer() {
        reset();
    }
    bool ready() const {
        return textures[0] != nullptr;
    }
    void reset() {
        for (auto &t : textures) {
            if (t)
                SDL_DestroyTexture(t);
            t = nullptr;
        }
        images = {};
        uploadBytes = 0;
        draws = 0;
        submitTicks = peakTicks = 0;
    }
    void load(SDL_Renderer *r, Pixels parts, Pixels final) {
        reset();
        Pixels mask = parts;
        for (size_t i = 0; i < mask.rgba.size(); i += 4)
            mask.rgba[i] = mask.rgba[i + 1] = mask.rgba[i + 2] = mask.rgba[i + 3];
        images = {std::make_shared<const Pixels>(std::move(mask)),
                  std::make_shared<const Pixels>(std::move(parts)),
                  std::make_shared<const Pixels>(std::move(final))};
        SDL_BlendMode modes[] = {SDL_BLENDMODE_MUL, SDL_BLENDMODE_ADD, SDL_BLENDMODE_BLEND};
        for (int i = 0; i < 3; ++i) {
            const auto &p = *images[i];
            textures[i] = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, p.w, p.h);
            if (!textures[i] || SDL_UpdateTexture(textures[i], nullptr, p.rgba.data(), p.w * 4) ||
                SDL_SetTextureBlendMode(textures[i], modes[i]))
                throw std::runtime_error(SDL_GetError());
            SDL_SetTextureScaleMode(textures[i], SDL_ScaleModeNearest);
            uploadBytes += p.rgba.size();
        }
    }
    void draw(SDL_Renderer *r, SnapshotRecorder &snapshots, float time, int directions, int variant) {
        const uint64_t begin = SDL_GetPerformanceCounter();
        if (!ready())
            throw std::runtime_error("Logo textures are not initialized");
        int gray = time < 4.4f     ? std::min(255, (int(time / .05f) + 1) * 3)
                   : time < 5.002f ? 255
                                   : std::max(0, 255 - (int((time - 5.002f) / .05f) + 1) * 4);
        SDL_Rect screen{0, 0, 800, 600};
        SDL_Color background{Uint8(gray), Uint8(gray), Uint8(gray), 255};
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, background.r, background.g, background.b, 255);
        snapshots.fill(r, screen, background);
        SDL_RenderFillRect(r, &screen);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        static const float durations[8][3] = {{2.2f, 2.2f, 2.2f},  {2.2f, 1.1f, 1.65f},   {2.2f, 1.65f, 1.1f},
                                              {1.1f, 2.2f, 1.65f}, {1.65f, 2.2f, 1.1f},   {1.1f, 1.65f, 2.2f},
                                              {1.65f, 1.1f, 2.2f}, {2.2f / 3, 2.2f, 2.2f}};
        float starts[3][2] = {{float(directions & 1 ? -700 : 800), 200},
                              {50, float(directions & 2 ? -240 : 800)},
                              {float(directions & 4 ? 50 : 800), float(directions & 4 ? 600 : -700)}};
        float controls[3][2] = {{120, 140}, {220, 220}, {float(directions & 4 ? -80 : 170), 220}};
        float colors[3][3] = {
            {32 / 128.f, 0, 111 / 128.f}, {0, 111 / 128.f, 32 / 128.f}, {111 / 128.f, 32 / 128.f, 0}};
        const auto &parts = *images[1];
        for (int layer = 0; layer < 3; ++layer) {
            if (!((time >= 2.2f && time < 4.4f) || (layer == 0 && time >= 4.4f && time < 4.7f)))
                continue;
            float t = std::clamp((time - 2.2f) / durations[variant & 7][layer], 0.f, 1.f);
            int x = int((1 - t) * (1 - t) * starts[layer][0] + 2 * (1 - t) * t * controls[layer][0] +
                        t * t * (120 + parts.x));
            int y = int((1 - t) * (1 - t) * starts[layer][1] + 2 * (1 - t) * t * controls[layer][1] +
                        t * t * (220 + parts.y));
            Uint8 k[3];
            for (int c = 0; c < 3; ++c)
                k[c] = Uint8(std::lround((time >= 4.4f ? 1.f : colors[layer][c] * t) * 255));
            sprite(r, snapshots, 0, x, y, {Uint8(255 - k[0]), Uint8(255 - k[1]), Uint8(255 - k[2]), 255});
            sprite(r, snapshots, 1, x, y, {k[0], k[1], k[2], 255});
        }
        if (time >= 4.7f && time < 5.902f) {
            float phase = std::clamp((time - 4.7f) / .3f, 0.f, 4.f), levels[] = {128, 48, 16, 3, 0};
            int part = std::min(3, int(phase));
            float alpha = (levels[part] + (levels[part + 1] - levels[part]) * (phase - part)) / 128;
            sprite(r, snapshots, 2, 120 + images[2]->x, 220 + images[2]->y,
                   {255, 255, 255, Uint8(alpha * 255)});
        }
        draws++;
        uint64_t elapsed = SDL_GetPerformanceCounter() - begin;
        submitTicks += elapsed;
        peakTicks = std::max(peakTicks, elapsed);
    }
};
} // namespace tenshi
