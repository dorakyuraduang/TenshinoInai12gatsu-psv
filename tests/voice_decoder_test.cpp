#define SDL_MAIN_HANDLED
#include "media.hpp"
#include "voice_decoder.hpp"
#include <atomic>
#include <cmath>
#include <iostream>
using namespace tenshi;
namespace {
std::atomic<unsigned> callbacks{0};
std::atomic<uint64_t> callbackLast{0}, callbackGap{0};
void postmix(void *, Uint8 *, int) {
    uint64_t now = SDL_GetPerformanceCounter();
    uint64_t before = callbackLast.exchange(now);
    if (before) {
        uint64_t gap = now - before, maximum = callbackGap.load();
        while (gap > maximum && !callbackGap.compare_exchange_weak(maximum, gap)) {}
    }
    callbacks++;
}
using Chunk = std::unique_ptr<Mix_Chunk, decltype(&Mix_FreeChunk)>;
void require(bool success, const char *message) {
    if (!success)
        throw std::runtime_error(message);
}
void rejects(const Bytes &bytes, int rate = 44100, SDL_AudioFormat format = AUDIO_S16SYS, int ch = 2) {
    bool rejected = false;
    try {
        Chunk result(decodeVoicePcm(bytes, rate, format, ch), Mix_FreeChunk);
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "Malformed input or output format accepted");
}
} // namespace
int main(int argc, char **argv) {
    bool audio = false;
    try {
        if (argc < 2 || argc > 3)
            throw std::runtime_error("Usage: voice_decoder_test <original-resources> [stress-clip]");
        SDL_SetMainReady();
        require(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) == 0, "SDL audio initialization failed");
        require((Mix_Init(MIX_INIT_OGG) & MIX_INIT_OGG) != 0, "SDL_mixer Ogg unavailable for comparison");
        require(Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 256) == 0, "Cannot open test mixer");
        audio = true;
        int rate = 0, ch = 0;
        Uint16 format = 0;
        require(Mix_QuerySpec(&rate, &format, &ch) != 0 && format == AUDIO_S16SYS,
                "Unexpected test mixer format");
        Archive voices(std::string(argv[1]) + "/voice.a");
        std::vector<std::string> names{"com_0100_001.g", "com_0100_002.g", "com_0100_003.g",
                                       "com_0100_004.g", "com_0100_005.g", "com_0100_006.g",
                                       "com_0100_007.g", "!asu_bonus.g", "!emi_bonus.g", "!mah_bonus.g",
                                       "!sin_bonus.g", "!toh_bonus.g", "!yuk_bonus.g"};
        size_t index = 0;
        for (const auto &entry : voices.entries) {
            if (index++ % std::max(size_t(1), voices.entries.size() / 12) == 0)
                names.push_back(entry.first);
        }
        for (const auto &name : names) {
            auto ogg = repairedVoice(voices.read(name));
            Chunk expected(Mix_LoadWAV_RW(SDL_RWFromConstMem(ogg.data(), int(ogg.size())), 1),
                           Mix_FreeChunk);
            require(bool(expected), "SDL_mixer reference decode failed");
            Chunk actual(decodeVoicePcm(ogg, rate, format, ch), Mix_FreeChunk);
            require(actual->alen == expected->alen, "Decoded sample count differs from SDL_mixer");
            auto *a = reinterpret_cast<const int16_t *>(actual->abuf);
            auto *b = reinterpret_cast<const int16_t *>(expected->abuf);
            int maximum = 0;
            double square = 0;
            size_t samples = actual->alen / sizeof(int16_t);
            for (size_t i = 0; i < samples; i++) {
                int difference = int(a[i]) - int(b[i]);
                maximum = std::max(maximum, std::abs(difference));
                square += double(difference) * difference;
            }
            double rms = std::sqrt(square / samples);
            std::cout << "PCM " << name << ": samples=" << samples << " max_difference=" << maximum
                      << " rms_difference=" << rms << "\n";
            // SDL_AudioCVT and SDL_AudioStream may round their resampler output differently.
            require(maximum <= 8 && rms <= 1, "Voice PCM differs materially from SDL_mixer");
            rejects(ogg, 0);
            rejects(ogg, rate, format, 0);
        }
        rejects({});
        rejects(Bytes(128, 0));
        auto stress = repairedVoice(voices.read(argc == 3 ? argv[2] : "!asu_bonus.g"));
        Mix_SetPostMix(postmix, nullptr);
        uint64_t baselineStart = SDL_GetPerformanceCounter();
        SDL_Delay(120);
        uint64_t baselineElapsed = SDL_GetPerformanceCounter() - baselineStart;
        unsigned baseline = callbacks.load();
        require(baseline >= 2, "Audio callback did not run before stress test");
        callbackGap = 0;
        unsigned startCallbacks = callbacks.load();
        uint64_t start = SDL_GetPerformanceCounter();
        unsigned loops = 0;
        do {
            Chunk result(decodeVoicePcm(stress, rate, format, ch), Mix_FreeChunk);
            loops++;
        } while (SDL_GetPerformanceCounter() - start < SDL_GetPerformanceFrequency() / 2);
        uint64_t elapsed = SDL_GetPerformanceCounter() - start;
        unsigned progressed = callbacks.load() - startCallbacks;
        double milliseconds = elapsed * 1000.0 / SDL_GetPerformanceFrequency();
        double gapMs = callbackGap.load() * 1000.0 / SDL_GetPerformanceFrequency();
        Mix_SetPostMix(nullptr, nullptr);
        std::cout << "CALLBACK independent decoder: loops=" << loops << " elapsed_ms=" << milliseconds
                  << " callbacks=" << progressed << " max_gap_ms=" << gapMs << "\n";
        require(progressed >= 2 && progressed * double(baselineElapsed) * 2 >= baseline * double(elapsed),
                "Audio callback rate dropped below half its idle baseline during Vorbis decode");
        Mix_CloseAudio();
        audio = false;
        Mix_Quit();
        SDL_Quit();
        std::cout << "Independent voice decoder: PCM parity, malformed input and live audio progress passed\n";
        return 0;
    } catch (const std::exception &e) {
        if (audio) {
            Mix_SetPostMix(nullptr, nullptr);
            Mix_CloseAudio();
        }
        Mix_Quit();
        SDL_Quit();
        std::cerr << e.what() << "\n";
        return 1;
    }
}
