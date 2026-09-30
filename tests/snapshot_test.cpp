#define SDL_MAIN_HANDLED
#include "frame_snapshot.hpp"
#include <iostream>
using namespace tenshi;
int main() {
    SDL_SetMainReady();
    SDL_Surface *target = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *texture = nullptr;
    try {
        if (SDL_Init(0))
            throw std::runtime_error(SDL_GetError());
        target = SDL_CreateRGBSurfaceWithFormat(0, 64, 48, 32, SDL_PIXELFORMAT_RGBA32);
        renderer = SDL_CreateSoftwareRenderer(target);
        if (!renderer)
            throw std::runtime_error(SDL_GetError());
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 8, 12, 20, 255);
        SDL_RenderClear(renderer);
        FrameSnapshot snapshot(64, 48);
        auto pixels = std::make_shared<Pixels>(11, 9);
        for (int y = 0; y < 9; y++)
            for (int x = 0; x < 11; x++) {
                size_t p = (y * 11 + x) * 4;
                pixels->rgba[p] = uint8_t(x * 21);
                pixels->rgba[p + 1] = uint8_t(y * 29);
                pixels->rgba[p + 2] = 123;
                pixels->rgba[p + 3] = uint8_t(32 + x * 15);
            }
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, 11, 9);
        SDL_UpdateTexture(texture, nullptr, pixels->rgba.data(), 44);
        SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_Color mod{221, 173, 244, 179};
        SDL_SetTextureColorMod(texture, mod.r, mod.g, mod.b);
        SDL_SetTextureAlphaMod(texture, mod.a);
        SDL_Rect destination{-3, 5, 33, 27}, clip{4, 8, 48, 33};
        SDL_RenderSetClipRect(renderer, &clip);
        SDL_RenderCopy(renderer, texture, nullptr, &destination);
        snapshot.texture(pixels, destination, clip, mod, SDL_BLENDMODE_BLEND);
        SDL_Rect fill{21, 19, 34, 12};
        SDL_Color color{83, 157, 201, 129};
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_RenderFillRect(renderer, &fill);
        snapshot.fill(fill, clip, color);
        SDL_RenderSetClipRect(renderer, nullptr);
        SDL_SetTextureAlphaMod(texture, 255);
        SDL_SetTextureColorMod(texture, 255, 255, 255);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
        SDL_Rect copy{42, 1, 11, 9};
        SDL_RenderCopy(renderer, texture, nullptr, &copy);
        snapshot.texture(pixels, copy, {0, 0, 64, 48}, {255, 255, 255, 255}, SDL_BLENDMODE_NONE);
        // The SDL software oracle has no Vita target-readback limitation. Production never calls it.
        Pixels expected(64, 48);
        if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, expected.rgba.data(), 256))
            throw std::runtime_error(SDL_GetError());
        auto actual = snapshot.render();
        int worst = 0;
        for (size_t p = 0; p < actual.rgba.size(); p++)
            worst = std::max(worst, std::abs(int(actual.rgba[p]) - int(expected.rgba[p])));
        if (worst > 3)
            throw std::runtime_error("CPU snapshot differs from SDL software renderer: " +
                                     std::to_string(worst));
        std::weak_ptr<const Pixels> lifetime = pixels;
        pixels.reset();
        if (lifetime.expired())
            throw std::runtime_error("Snapshot lost its source after cache eviction");
        if (snapshot.render().rgba != actual.rgba)
            throw std::runtime_error("Snapshot mutated after releasing texture source");
        std::cout << "Snapshot regression passed: clip, scaling, color/alpha modulation, blends, source "
                     "lifetime; max channel difference "
                  << worst << "\n";
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(target);
        SDL_Quit();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        if (texture)
            SDL_DestroyTexture(texture);
        if (renderer)
            SDL_DestroyRenderer(renderer);
        if (target)
            SDL_FreeSurface(target);
        SDL_Quit();
        return 1;
    }
}
