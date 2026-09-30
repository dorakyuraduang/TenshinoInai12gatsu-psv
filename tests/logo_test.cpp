#define SDL_MAIN_HANDLED
#include "logo.hpp"
#include "logo_reference.hpp"
#include "presentation.hpp"
#include <iostream>
using namespace tenshi;
int main(int argc, char **argv) {
    SDL_SetMainReady();
    SDL_Surface *surface = nullptr;
    SDL_Renderer *r = nullptr;
    try {
        if (argc != 2)
            throw std::runtime_error("Pass original resource directory");
        SDL_Init(0);
        surface = SDL_CreateRGBSurfaceWithFormat(0, 800, 600, 32, SDL_PIXELFORMAT_RGBA32);
        r = SDL_CreateSoftwareRenderer(surface);
        if (!r)
            throw std::runtime_error(SDL_GetError());
        Archive sys(std::string(argv[1]) + "/sys.a");
        auto atlas = lzss(sys.read("leaflogo.px"));
        auto parts = pxFrame(atlas, 0), final = pxFrame(atlas, 1);
        LogoRenderer logo;
        logo.load(r, parts, final);
        const auto uploads = logo.uploadBytes;
        SnapshotRecorder recorder;
        Pixels gpu(800, 600);
        int worst = 0, snapshotWorst = 0, samples = 0;
        uint64_t difference = 0, channels = 0;
        float phases[] = {0,      2.19f, 2.2f,  2.25f, 2.6f,   2.933f, 3.299f, 3.7f, 4.399f, 4.4f,
                          4.699f, 4.7f,  4.85f, 5.f,   5.002f, 5.3f,   5.599f, 5.9f, 5.902f, 9.002f};
        for (int d = 0; d < 8; ++d)
            for (int v = 0; v < 8; ++v)
                for (float time : phases) {
                    recorder.begin(nullptr);
                    logo.draw(r, recorder, time, d, v);
                    recorder.finish();
                    if (SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGBA32, gpu.rgba.data(), 800 * 4))
                        throw std::runtime_error(SDL_GetError());
                    auto before = logoReference(parts, final, time, d, v);
                    for (size_t i = 0; i < gpu.rgba.size(); ++i) {
                        int delta = std::abs(int(gpu.rgba[i]) - int(before.rgba[i]));
                        worst = std::max(worst, delta);
                        difference += delta;
                        ++channels;
                    }
                    if (v == 0) {
                        auto cpu = recorder.last.render();
                        for (size_t i = 0; i < cpu.rgba.size(); ++i)
                            snapshotWorst =
                                std::max(snapshotWorst, std::abs(int(cpu.rgba[i]) - int(gpu.rgba[i])));
                    }
                    ++samples;
                    if (logo.uploadBytes != uploads)
                        throw std::runtime_error("Logo uploaded textures after warm-up");
                }
        std::cout << "Logo: " << samples << " frames, all 64 variants; old/new max channel difference "
                  << worst << ", mean " << double(difference) / channels << "; snapshot difference "
                  << snapshotWorst << "; one-time upload " << uploads << ", per-frame upload 0 bytes\n";
        if (worst > 8 || snapshotWorst > 8)
            throw std::runtime_error("Logo differs beyond blend quantization tolerance");
        logo.reset();
        SDL_DestroyRenderer(r);
        SDL_FreeSurface(surface);
        SDL_Quit();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
