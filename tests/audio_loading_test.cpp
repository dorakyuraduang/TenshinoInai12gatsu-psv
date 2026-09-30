#define SDL_MAIN_HANDLED
#include "prefetched_audio.hpp"
#include "voice_decoder.hpp"
#include <atomic>
#include <iostream>

using namespace tenshi;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::atomic<unsigned> callbacks{0};
std::atomic<uint64_t> lastCallback{0}, longestGap{0};
void postmix(void *, Uint8 *, int) {
    uint64_t now = SDL_GetPerformanceCounter();
    uint64_t before = lastCallback.exchange(now);
    if (before) {
        uint64_t gap = now - before, maximum = longestGap.load();
        while (gap > maximum && !longestGap.compare_exchange_weak(maximum, gap)) {}
    }
    ++callbacks;
}
struct DiskProbe {
    std::atomic<unsigned> reads{0}, active{0};
    std::atomic<bool> slow{false};
};
using Chunk = std::unique_ptr<Mix_Chunk, decltype(&Mix_FreeChunk)>;
struct Track {
    SDL_RWops *rw = nullptr; // Borrowed only while music owns and keeps it alive.
    Mix_Music *music = nullptr;
    std::shared_ptr<DiskProbe> disk = std::make_shared<DiskProbe>();
    Track(const Archive &archive, const std::string &name) {
        PrefetchedArchiveWave::Options options;
        auto probe = disk;
        options.beforeRead = [probe] {
            ++probe->active;
            ++probe->reads;
            if (probe->slow)
                SDL_Delay(120); // Slower than one mixer buffer; cached PCM must cover it.
            --probe->active;
        };
        rw = PrefetchedArchiveWave::open(archive, name, std::move(options));
        music = Mix_LoadMUS_RW(rw, 1);
        if (!music) {
            // Mix_LoadMUS_RW with freesrc=1 closes the RWops on failure.
            rw = nullptr;
            throw std::runtime_error(std::string("Cannot load prefetched music: ") + Mix_GetError());
        }
    }
    ~Track() {
        if (music)
            Mix_FreeMusic(music); // SDL_mixer closes RWops; its destructor joins the reader.
    }
    Track(const Track &) = delete;
    Track &operator=(const Track &) = delete;
    PrefetchedArchiveWave::Stats stats() {
        return PrefetchedArchiveWave::get(rw).stats();
    }
    void close() {
        if (music) {
            Mix_FreeMusic(music);
            music = nullptr;
            rw = nullptr;
        }
        check(disk->active == 0, "Old music freed before prefetch worker joined");
        unsigned reads = disk->reads;
        SDL_Delay(70);
        check(reads == disk->reads, "Old prefetch worker accessed its source after free");
    }
};
struct AudioLifetime {
    bool opened = false;
    ~AudioLifetime() {
        if (opened) {
            Mix_SetPostMix(nullptr, nullptr);
            Mix_HaltChannel(-1);
            Mix_HaltMusic();
            Mix_CloseAudio();
        }
        Mix_Quit();
        SDL_Quit();
    }
};
void healthy(const PrefetchedArchiveWave::Stats &stats) {
    check(stats.underrunBytes == 0 && stats.ioErrors == 0, "Normal music playback missed cached PCM");
    check(stats.bufferBytes <= 4 * PrefetchedArchiveWave::PageBytes, "Music PCM cache exceeded four pages");
}
} // namespace

int main(int argc, char **argv) {
    AudioLifetime audio;
    try {
        check(argc == 2, "Usage: audio_loading_test <original-resources>");
        SDL_SetMainReady();
        check(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) == 0, "SDL initialization failed");
        check(Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 2048) == 0, "Cannot open integration-test mixer");
        audio.opened = true;
        Mix_AllocateChannels(10);
        int frequency = 0, channels = 0;
        Uint16 format = 0;
        check(Mix_QuerySpec(&frequency, &format, &channels) != 0 && frequency == 44100 &&
                  format == AUDIO_S16SYS && channels == 2,
              "Unexpected integration-test mixer format");
        const std::string root = argv[1];
        Archive music(root + "/music.a"), voices(root + "/voice.a"),
                backgrounds(root + "/egbg.a"), characters(root + "/char.a");
        Track first(music, "003.w");
        Mix_SetPostMix(postmix, nullptr);
        check(Mix_PlayMusic(first.music, -1) == 0, "Cannot start prefetched music");
        uint64_t baselineStart = SDL_GetPerformanceCounter();
        SDL_Delay(250);
        uint64_t baselineTicks = SDL_GetPerformanceCounter() - baselineStart;
        unsigned baseline = callbacks.load();
        check(baseline >= 3, "Baseline audio callback did not progress");
        first.disk->slow = true;
        longestGap = 0;
        uint64_t start = SDL_GetPerformanceCounter();
        unsigned before = callbacks.load(), loads = 0;
        uint64_t pixelsProcessed = 0;
        Chunk voice(nullptr, Mix_FreeChunk);
        const char *backgroundNames[] = {"bg001.px", "bg002.px", "bg003.px", "bg004.px", "bg005.px", "bg006.px"};
        const char *characterNames[] = {"asu101.px", "asu102.px", "asu103.px", "asu104.px", "asu105.px", "asu106.px"};
        const char *voiceNames[] = {"com_0100_001.g", "com_0100_002.g", "com_0100_003.g", "com_0100_004.g",
                                  "com_0100_005.g", "com_0100_006.g", "com_0100_007.g"};
        // Six seconds at 44.1 kHz stereo crosses the first default 1 MiB page,
        // exercising real SDL_mixer reads while its next page loads on the worker.
        while (SDL_GetPerformanceCounter() - start < SDL_GetPerformanceFrequency() * 13 / 2) {
            auto backgroundImage = background(backgrounds, backgroundNames[loads % 6]);
            auto characterImage = pxStandalone(characters.read(characterNames[loads % 6]));
            pixelsProcessed += backgroundImage.rgba.size() + characterImage.rgba.size();
            auto ogg = repairedVoice(voices.read(voiceNames[loads % 7]));
            Chunk next(decodeVoicePcm(ogg, frequency, format, channels), Mix_FreeChunk);
            Mix_HaltChannel(0);
            voice = std::move(next);
            check(Mix_PlayChannel(0, voice.get(), 0) == 0, "Cannot play independently decoded voice");
            check(Mix_PlayingMusic() != 0, "Resource loading caused false music EOF");
            ++loads;
            SDL_Delay(35);
        }
        uint64_t elapsed = SDL_GetPerformanceCounter() - start;
        unsigned progressed = callbacks.load() - before;
        check(progressed * double(baselineTicks) * 2 >= baseline * double(elapsed),
              "Loading images and voices blocked the audio callback");
        check(first.disk->reads >= 3, "Playback never exercised rolling-page prefetch");
        auto firstStats = first.stats();
        healthy(firstStats);
        std::cout << "LOADING mixer=44100/S16/stereo/2048 image_pairs=" << loads << " voices=" << loads
                  << " decoded_image_bytes=" << pixelsProcessed << " callbacks=" << progressed
                  << " max_gap_ms=" << longestGap.load() * 1000.0 / SDL_GetPerformanceFrequency()
                  << " background_page_reads=" << first.disk->reads << " underrun_bytes=" << firstStats.underrunBytes
                  << " io_errors=" << firstStats.ioErrors << "\n";

        Mix_PauseMusic();
        Mix_Pause(0);
        check(Mix_PausedMusic() && Mix_Paused(0), "Pause was not applied to music and voice");
        Sint64 pausedAt = SDL_RWtell(first.rw);
        before = callbacks.load();
        SDL_Delay(200);
        check(SDL_RWtell(first.rw) == pausedAt, "Music consumed PCM while paused");
        check(callbacks.load() > before && Mix_PlayingMusic(), "Paused music stopped audio callbacks or reported EOF");
        Mix_ResumeMusic();
        Mix_Resume(0);
        check(!Mix_PausedMusic() && !Mix_Paused(0), "Resume was not applied");
        SDL_Delay(180);
        check(SDL_RWtell(first.rw) > pausedAt && Mix_PlayingMusic(), "Music did not resume PCM consumption");
        healthy(first.stats());

        // Preparing the replacement source and freeing the old owned RWops must
        // preserve mixer lifecycle and leave no file reader behind.
        Track second(music, "006.w");
        healthy(second.stats());
        Mix_HaltMusic();
        first.close();
        check(Mix_PlayMusic(second.music, -1) == 0, "Cannot play replacement music");
        SDL_Delay(220);
        check(Mix_PlayingMusic() && !Mix_PausedMusic(), "Replacement music unexpectedly stopped");
        healthy(second.stats());
        Track third(music, "021.w");
        Mix_HaltMusic();
        second.close();
        check(Mix_PlayMusic(third.music, -1) == 0, "Cannot play second replacement music");
        SDL_Delay(220);
        check(Mix_PlayingMusic(), "Second replacement unexpectedly stopped");
        healthy(third.stats());
        Mix_HaltMusic();
        third.close();
        Mix_HaltChannel(0);
        voice.reset();
        std::cout << "LIFECYCLE pause/resume and two music replacements passed; all normal underruns=0; old RWops readers joined\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
