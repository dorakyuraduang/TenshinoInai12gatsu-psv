#define SDL_MAIN_HANDLED
#include "frame_snapshot.hpp"
#include "logo.hpp"
#include "media.hpp"
#include "presentation.hpp"
#include "runtime.hpp"
#include "weather.hpp"
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <memory>
#include <set>
#ifdef __vita__
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
extern "C" {
unsigned int _newlib_heap_size_user = 160 * 1024 * 1024;
}
#endif
using namespace tenshi;
static bool exists(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return bool(f);
}
static std::string number(int n, int digits = 3) {
    char b[32];
    snprintf(b, sizeof(b), "%0*d", digits, n);
    return b;
}
static std::string replaceAll(std::string s, const std::string &a, const std::string &b) {
    size_t p = 0;
    while ((p = s.find(a, p)) != std::string::npos) {
        s.replace(p, a.size(), b);
        p += b.size();
    }
    return s;
}
static SnapshotRecorder snapshots;
struct Texture {
    // CPU copy for snapshots. Shared, never mutated through this pointer; callers that pass an rvalue
    // or a shared buffer avoid a full-image copy (1.9 MB per 800x600 upload).
    std::shared_ptr<const Pixels> pixels;
    std::shared_ptr<const FrameSnapshot> nested; // lazily rasterized source (save previews)
    SDL_Texture *ptr = nullptr;
    int w = 0, h = 0, originX = 0, originY = 0;
    Texture() = default;
    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;
    ~Texture() {
        reset();
    }
    void reset() {
        if (ptr)
            SDL_DestroyTexture(ptr);
        ptr = nullptr;
        pixels.reset();
        nested.reset();
    }
    void load(SDL_Renderer *r, std::shared_ptr<const Pixels> image) {
        reset();
        w = image->w;
        h = image->h;
        originX = image->x;
        originY = image->y;
        ptr = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
        if (!ptr)
            throw std::runtime_error(SDL_GetError());
        if (SDL_UpdateTexture(ptr, nullptr, image->rgba.data(), w * 4))
            throw std::runtime_error(SDL_GetError());
        pixels = std::move(image);
        SDL_SetTextureBlendMode(ptr, SDL_BLENDMODE_BLEND);
    }
    void load(SDL_Renderer *r, const Pixels &image) {
        load(r, std::make_shared<const Pixels>(image));
    }
    void load(SDL_Renderer *r, Pixels &&image) {
        load(r, std::make_shared<const Pixels>(std::move(image)));
    }
    void update(std::shared_ptr<const Pixels> image) {
        if (image->w != w || image->h != h)
            throw std::runtime_error("Texture update geometry mismatch");
        if (SDL_UpdateTexture(ptr, nullptr, image->rgba.data(), w * 4))
            throw std::runtime_error(std::string("Texture update: ") + SDL_GetError());
        pixels = std::move(image);
    }
    void update(const Pixels &image) {
        update(std::make_shared<const Pixels>(image));
    }
    void update(Pixels &&image) {
        update(std::make_shared<const Pixels>(std::move(image)));
    }
    void loadSurface(SDL_Renderer *r, SDL_Surface *source) {
        auto *rgba = SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_RGBA32, 0);
        if (!rgba)
            throw std::runtime_error(SDL_GetError());
        Pixels image(rgba->w, rgba->h);
        for (int y = 0; y < rgba->h; y++)
            std::memcpy(image.rgba.data() + size_t(y) * rgba->w * 4,
                        static_cast<const uint8_t *>(rgba->pixels) + y * rgba->pitch, rgba->w * 4);
        SDL_FreeSurface(rgba);
        load(r, std::move(image));
    }
    void draw(SDL_Renderer *r, int x, int y, int width = 0, int height = 0) {
        if (ptr) {
            SDL_Rect dst{x, y, width ? width : w, height ? height : h};
            snapshots.texture(r, ptr, pixels, dst, nested);
            SDL_RenderCopy(r, ptr, nullptr, &dst);
        }
    }
};
// A small ASCII fallback, so missing player-supplied fonts can still be diagnosed.
static void fallback(SDL_Renderer *r, const std::string &s, int x, int y) {
    static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:/-_ ";
    static const std::array<std::string, 42> glyph = {
        "01110100011000111111100011000110001", "11110100011000111110100011000111110",
        "01111100001000010000100001000001111", "11110100011000110001100011000111110",
        "11111100001000011110100001000011111", "11111100001000011110100001000010000",
        "01111100001000010111100011000101111", "10001100011000111111100011000110001",
        "11111001000010000100001000010011111", "00111000100001000010100101001001100",
        "10001100101010011000101001001010001", "10000100001000010000100001000011111",
        "10001110111010110101100011000110001", "10001110011010110011100011000110001",
        "01110100011000110001100011000101110", "11110100011000111110100001000010000",
        "01110100011000110001101011001001101", "11110100011000111110101001001010001",
        "01111100001000001110000010000111110", "11111001000010000100001000010000100",
        "10001100011000110001100011000101110", "10001100011000110001100010101000100",
        "10001100011000110101101011101110001", "10001100010101000100010101000110001",
        "10001100010101000100001000010000100", "11111000010001000100010001000011111",
        "01110100011001110101110011000101110", "00100011000010000100001000010001110",
        "01110100010000100010001000100011111", "11110000010000101110000010000111110",
        "00010001100101010010111110001000010", "11111100001000011110000010000111110",
        "01110100001000011110100011000101110", "11111000010001000100010000100001000",
        "01110100011000101110100011000101110", "01110100011000101111000010000101110",
        "00000000000000000000000000011000110", "00000001100011000000001100011000000",
        "00001000100001000100010000100010000", "00000000000000011111000000000000000",
        "00000000000000000000000000000011111", "00000000000000000000000000000000000"};
    int cx = x, cy = y;
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    for (unsigned char ch : s) {
        if (ch == '\n' || cx > 760) {
            cx = x;
            cy += 20;
            if (ch == '\n')
                continue;
        }
        char c = char(std::toupper(ch));
        auto at = alphabet.find(c);
        if (at == std::string::npos)
            at = 41;
        auto &g = glyph[at];
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (g[row * 5 + col] == '1') {
                    SDL_Rect q{cx + col * 2, cy + row * 2, 2, 2};
                    SDL_RenderFillRect(r, &q);
                }
        cx += 12;
    }
}
#include "archive_audio.hpp"
#include "voice_decoder.hpp"
#include "prefetched_audio.hpp"
#include "movie.hpp"
class App {
    Movie movie;
    std::map<std::string, std::unique_ptr<Texture>> textCache;
    bool ending = false;
    uint32_t endingAt = 0;
    int endingRoute = 1;
    Json endingInfo;
    std::map<int, std::unique_ptr<Texture>> endingTextures;
    std::string root, stateRoot;
    std::map<std::string, std::unique_ptr<Archive>> archives;
    Bytes endingCredits;
    Pixels endingStrip;
    Archive &asset(const std::string &name) {
        if (!archives.count(name))
            archives[name] = std::make_unique<Archive>(root + "/" + name);
        return *archives.at(name);
    }
    void prepareEnding() {
        static const char *routes[] = {"toh", "sin", "asu", "yuk", "mah"};
        endingCredits = lzss(asset("ed.a").read("ending.px"));
        endingStrip = pxStandalone(asset("ed.a").read(std::string("ed_") + routes[endingRoute - 1] + ".px"));
        auto lines = OriginalScript(asset("tenshi_dvd.a"), "@ending.p").strings;
        if (lines.size() > 3)
            lines.resize(3);
        endingInfo = {{"lines", lines}};
        endingTextures.clear();
    }

    SDL_Window *window = nullptr;
    SDL_Renderer *r = nullptr;
    TTF_Font *font = nullptr;
    SDL_GameController *pad = nullptr;
    std::unique_ptr<Runtime> vm;
    Texture title;
    Mix_Music *music = nullptr;
    SDL_RWops *musicStream = nullptr; // Non-owning: Mix_FreeMusic closes it.
    uint64_t musicUnderrunBytes = 0, musicIoErrors = 0;
    uint32_t musicStatsAt = 0;
    Mix_Chunk *outgoingMusic = nullptr;
    int loadedMusicId = 0;
    bool musicLoops = true;
    uint32_t musicStartedAt = 0;
    Mix_Chunk *voice = nullptr;
    std::array<Mix_Chunk *, 8> sounds{};
    std::array<int, 8> soundIds{};
    int soundChannel = 0, musicId = -1;
    bool audio = false, running = true, menu = true, inGame = false, hidden = false, historyMode = false,
         autoMode = false;
    std::string screenshotPath;
    int selected = 0, slot = 1, choice = 0, page = 0, historyAt = 0, volume = 96;
    uint32_t deadline = 0, autoAt = 0, fadeUntil = 0;
    int tone = 0, weather = 0;
    std::string notice, fatal, surname = "木田", given = "时纪";
    std::vector<std::string> pages, history;
    Json visual = {{"background", -1},
                   {"characters", {0, 0, 0}},
                   {"previous", {0, 0, 0}},
                   {"count", 0},
                   {"speaker", 255},
                   {"text", ""},
                   {"full", false},
                   {"window", true},
                   {"clear", true},
                   {"choice", Json::array()},
                   {"music", 0},
                   {"voice", ""},
                   {"weather", 0},
                   {"tone", 0}};
#include "interface.inc"
    void rectangle(int x, int y, int w, int h, SDL_Color c) {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_Rect q{x, y, w, h};
        SDL_RenderFillRect(r, &q);
        snapshots.fill(r, q, c);
    }
    std::string openingPath() const {
#ifdef __vita__
        return "app0:/assets/opening.mp4";
#else
        return root + "/openning.v"; // Host reference backend exercises the original source.
#endif
    }
    std::string fontPath(const std::string &name) const {
        if (name != "font.ttf")
            return stateRoot + "/" + name;
#ifdef __vita__
        return "app0:assets/SourceHanSansCN-Regular.otf";
#else
        char *base = SDL_GetBasePath();
        if (!base)
            throw std::runtime_error(std::string("Cannot locate bundled font: ") + SDL_GetError());
        std::string path = std::string(base) + "assets/SourceHanSansCN-Regular.otf";
        SDL_free(base);
        return path;
#endif
    }
    // Each font file is read once; every size is opened from that memory. Opening the 8 MiB CFF font
    // straight from a file costs ~26k small reads per size, which takes seconds through Vita sceIo.
    // Entries must outlive the TTF_Font objects, which the destructor closes first.
    std::map<std::string, std::unique_ptr<const Bytes>> fontFiles;
    TTF_Font *openFont(const std::string &name, int size) {
        const std::string path = fontPath(name);
        auto cached = fontFiles.find(path);
        if (cached == fontFiles.end()) {
            uint32_t started = SDL_GetTicks();
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
                throw std::runtime_error("FONT FILE NOT FOUND: " + path);
            auto bytes = file.tellg();
            auto data = std::make_unique<Bytes>(bytes > 0 ? size_t(bytes) : 0);
            file.seekg(0);
            if (bytes > 0 &&
                !file.read(reinterpret_cast<char *>(data->data()), std::streamsize(data->size())))
                throw std::runtime_error("FONT FILE FOUND BUT LOAD FAILED: " + path + " (" +
                                         std::to_string(static_cast<long long>(bytes)) +
                                         " bytes): read error");
            cached = fontFiles.emplace(path, std::move(data)).first;
            if (SDL_GetTicks() - started >= 50)
                perfLog("font file read", SDL_GetTicks() - started);
        }
        const Bytes &data = *cached->second;
        auto *result = TTF_OpenFontRW(SDL_RWFromConstMem(data.data(), int(data.size())), 1, size);
        if (!result)
            throw std::runtime_error("FONT FILE FOUND BUT LOAD FAILED: " + path + " (" +
                                     std::to_string(static_cast<long long>(data.size())) +
                                     " bytes): " + TTF_GetError());
        return result;
    }
    std::string formatted(std::string s) {
        s = replaceAll(s, "\xee\x80\x80", given);
        s = replaceAll(s, "\xee\x80\x81", surname);
        return s;
    }
    void paginate() {
        message.set(formatted(visual.at("text")), visual.at("full"));
        pages.assign(message.pages.size(), "");
        page = 0;
        revealed = 0;
        messageTimer = 0;
    }
    // Reading and Vorbis decoding both stay on the worker. SDL_mixer's Ogg loader holds
    // the live audio lock during decoding, so decode directly to the cached mixer format.
    // Only the latest request (voiceRequest) is ever played by the main thread.
    struct VoiceJob {
        unsigned id = 0;
        std::string name;
        const Archive *archive = nullptr; // App owns this until after the worker joins.
        Mix_Chunk *chunk = nullptr;
        std::string error;
        uint32_t ms = 0;
    };
    SDL_Thread *voiceThread = nullptr;
    SDL_mutex *voiceMutex = nullptr;
    SDL_cond *voiceWake = nullptr;
    std::vector<VoiceJob> voicePending, voiceDone;
    bool voiceQuit = false;
    unsigned voiceSerial = 0;
    std::atomic<unsigned> voiceRequest{0};
    int audioFrequency = 44100, audioChannels = 2;
    SDL_AudioFormat audioFormat = AUDIO_S16SYS;
    static int voiceWorker(void *user) {
        auto &app = *static_cast<App *>(user);
        SDL_LockMutex(app.voiceMutex);
        while (!app.voiceQuit) {
            if (app.voicePending.empty()) {
                SDL_CondWait(app.voiceWake, app.voiceMutex);
                continue;
            }
            VoiceJob job = std::move(app.voicePending.back());
            app.voicePending.clear();
            SDL_UnlockMutex(app.voiceMutex);
            uint32_t started = SDL_GetTicks();
            try {
                auto ogg = repairedVoice(job.archive->read(job.name));
                job.chunk = decodeVoicePcm(ogg, app.audioFrequency, app.audioFormat, app.audioChannels);
            } catch (const std::exception &e) {
                job.error = e.what();
            }
            job.ms = SDL_GetTicks() - started;
            if (job.id != app.voiceRequest.load()) {
                // Do not retain a cancelled long bonus clip during the next decode.
                if (job.chunk)
                    Mix_FreeChunk(job.chunk);
                SDL_LockMutex(app.voiceMutex);
                continue;
            }
            SDL_LockMutex(app.voiceMutex);
            app.voiceDone.push_back(std::move(job));
        }
        SDL_UnlockMutex(app.voiceMutex);
        return 0;
    }
    void stopVoiceWorker() {
        if (!voiceMutex)
            return;
        SDL_LockMutex(voiceMutex);
        voiceQuit = true;
        voicePending.clear();
        SDL_CondSignal(voiceWake);
        SDL_UnlockMutex(voiceMutex);
        SDL_WaitThread(voiceThread, nullptr);
        for (auto &job : voiceDone)
            if (job.chunk)
                Mix_FreeChunk(job.chunk);
        voiceDone.clear();
        SDL_DestroyCond(voiceWake);
        SDL_DestroyMutex(voiceMutex);
        voiceThread = nullptr;
        voiceWake = nullptr;
        voiceMutex = nullptr;
    }
    void stopVoice() {
        if (audio)
            Mix_HaltChannel(0);
        if (voice)
            Mix_FreeChunk(voice);
        voice = nullptr;
        voiceRequest = 0; // a clip still being decoded is discarded when it arrives
        if (voiceMutex) {
            SDL_LockMutex(voiceMutex);
            voicePending.clear();
            SDL_UnlockMutex(voiceMutex);
        }
    }
    void playVoice(const std::string &name) {
        stopVoice();
        if (!audio || name.empty())
            return;
        const Archive &voices = asset("voice.a");
        voices.entry(name); // Validate cheaply; the worker performs the file I/O.
        if (!voiceMutex) {
            voiceMutex = SDL_CreateMutex();
            voiceWake = SDL_CreateCond();
            if (voiceMutex && voiceWake)
                voiceThread = SDL_CreateThreadWithStackSize(voiceWorker, "voice-decode", 256 * 1024, this);
            if (!voiceThread) {
                std::string error = SDL_GetError();
                if (voiceWake)
                    SDL_DestroyCond(voiceWake);
                if (voiceMutex)
                    SDL_DestroyMutex(voiceMutex);
                voiceWake = nullptr;
                voiceMutex = nullptr;
                throw std::runtime_error("Cannot start voice decoder: " + error);
            }
        }
        voiceRequest = ++voiceSerial;
        SDL_LockMutex(voiceMutex);
        voicePending.push_back({voiceRequest, name, &voices, nullptr, {}});
        SDL_CondSignal(voiceWake);
        SDL_UnlockMutex(voiceMutex);
    }
    // Main thread, once per frame: start the requested clip when its decode has finished.
    void pollVoice() {
        if (!voiceMutex)
            return;
        std::vector<VoiceJob> done;
        SDL_LockMutex(voiceMutex);
        done.swap(voiceDone);
        SDL_UnlockMutex(voiceMutex);
        std::string failure;
        for (auto &job : done) {
            if (job.id != voiceRequest || !audio) {
                if (job.chunk)
                    Mix_FreeChunk(job.chunk);
                continue;
            }
            voiceRequest = 0;
            if (job.ms >= 150)
                perfLog("voice decode (background)", job.ms);
            if (!job.chunk) {
                failure = "Voice decode failed: " + job.name + " " + job.error;
                continue;
            }
            voice = job.chunk;
            Mix_PlayChannel(0, voice, 0);
        }
        if (!failure.empty())
            throw std::runtime_error(failure);
    }
    bool voiceBusy() {
        return voiceRequest != 0 || (audio && Mix_Playing(0));
    }
    // Self-tests and audits: block until the requested clip has been decoded and started.
    void finishVoiceDecode() {
        uint32_t started = SDL_GetTicks();
        while (voiceRequest && SDL_GetTicks() - started < 10000) {
            pollVoice();
            if (voiceRequest)
                SDL_Delay(1);
        }
    }
    void clearMusicOverlap() {
        overlapFade.duration = 0;
        if (audio)
            Mix_HaltChannel(9);
        if (outgoingMusic)
            Mix_FreeChunk(outgoingMusic);
        outgoingMusic = nullptr;
    }
    void stopMusic() {
        musicFade.duration = 0;
        clearMusicOverlap();
        if (audio)
            Mix_HaltMusic();
        musicId = 0;
    }
    void playMusic(int id, int fadeUnits = -1, bool loop = true) {
        if (id == musicId && (id <= 0 || loop == musicLoops))
            return;
        musicId = id;
        if (!audio)
            return;
        clearMusicOverlap();
        musicFade.duration = 0;
        if (id <= 0) {
            musicFade = {Mix_VolumeMusic(-1), 0, SDL_GetTicks(), uint32_t(std::max(1, fadeUnits) * 10)};
            return;
        }
        // Prefill on the main thread while the old track continues. Once playing,
        // SDL_mixer reads only cached PCM; it never seeks or reads the archive.
        std::unique_ptr<SDL_RWops, decltype(&SDL_RWclose)> nextStream(
            PrefetchedArchiveWave::open(asset("music.a"), number(id) + ".w"), SDL_RWclose);
        if (music && Mix_PlayingMusic()) {
            double position = Mix_GetMusicPosition(music);
            if (position < 0)
                position = (SDL_GetTicks() - musicStartedAt) / 1000.;
            auto bytes =
                musicOverlap(asset("music.a"), number(loadedMusicId) + ".w", position, .3, musicLoops);
            if (!bytes.empty()) {
                outgoingMusic = Mix_LoadWAV_RW(SDL_RWFromConstMem(bytes.data(), int(bytes.size())), 1);
                if (!outgoingMusic)
                    throw std::runtime_error(std::string("Music overlap decode failed: ") + Mix_GetError());
                overlapFade = {Mix_VolumeMusic(-1), 0, SDL_GetTicks(), 300};
            }
        }
        Mix_HaltMusic();
        if (outgoingMusic) {
            overlapFade.start = SDL_GetTicks();
            Mix_Volume(9, overlapFade.volume);
            Mix_PlayChannel(9, outgoingMusic, 0);
        }
        musicStream = nullptr;
        if (music)
            Mix_FreeMusic(music); // SDL_mixer unlocks audio before closing/joining the RW worker.
        SDL_RWops *prepared = nextStream.release();
        music = Mix_LoadMUS_RW(prepared, 1);
        if (!music)
            throw std::runtime_error(std::string("Music decode failed: ") + Mix_GetError());
        musicStream = prepared;
        musicUnderrunBytes = musicIoErrors = 0;
        musicStatsAt = SDL_GetTicks();
        loadedMusicId = id;
        musicLoops = loop;
        Mix_VolumeMusic(preferences.at("master").get<int>() * preferences.at("music").get<int>() * 128 / 400);
        musicStartedAt = SDL_GetTicks();
        Mix_PlayMusic(music, loop ? -1 : 0);
    }
    void pollMusicStream() {
        if (!musicStream || SDL_GetTicks() - musicStatsAt < 1000)
            return;
        musicStatsAt = SDL_GetTicks();
        auto stats = PrefetchedArchiveWave::get(musicStream).stats();
        if ((stats.underrunBytes != musicUnderrunBytes || stats.ioErrors != musicIoErrors) &&
            perfLines < 80) {
            fprintf(stderr, "Audio: music %03d cache underrun=%u B, read errors=%u, buffer=%u B\n",
                    loadedMusicId, unsigned(stats.underrunBytes), unsigned(stats.ioErrors),
                    unsigned(stats.bufferBytes));
            ++perfLines;
        }
        musicUnderrunBytes = stats.underrunBytes;
        musicIoErrors = stats.ioErrors;
    }
    // Loading a sound effect reads it from se.a and resamples 22.05 kHz to the 44.1 kHz mixer on the
    // main thread, so decoded chunks are kept (LRU, 16 MiB) and repeated effects start instantly.
    // The cache owns every chunk; sounds[] only records what each channel last played.
    struct SoundCached {
        Mix_Chunk *chunk = nullptr;
        uint64_t used = 0;
    };
    std::map<int, SoundCached> soundCache;
    size_t soundCacheBytes = 0;
    uint64_t soundCounter = 0;
    Mix_Chunk *soundChunk(int id, const std::string &p) {
        auto it = soundCache.find(id);
        if (it == soundCache.end()) {
            uint32_t started = SDL_GetTicks();
            Mix_Chunk *chunk = Mix_LoadWAV_RW(ArchiveWave::open(asset("se.a"), p), 1);
            if (!chunk)
                return nullptr;
            if (SDL_GetTicks() - started >= 50)
                perfLog("sound effect load", SDL_GetTicks() - started);
            while (soundCacheBytes + chunk->alen > 16u * 1024 * 1024) {
                auto oldest = soundCache.end();
                for (auto c = soundCache.begin(); c != soundCache.end(); ++c) {
                    bool playing = false;
                    for (int i = 0; i < 8; i++)
                        playing |= sounds[i] == c->second.chunk && Mix_Playing(i + 1);
                    if (!playing && (oldest == soundCache.end() || c->second.used < oldest->second.used))
                        oldest = c;
                }
                if (oldest == soundCache.end())
                    break;
                for (auto &s : sounds)
                    if (s == oldest->second.chunk)
                        s = nullptr;
                soundCacheBytes -= oldest->second.chunk->alen;
                Mix_FreeChunk(oldest->second.chunk);
                soundCache.erase(oldest);
            }
            soundCacheBytes += chunk->alen;
            it = soundCache.emplace(id, SoundCached{chunk, 0}).first;
        }
        it->second.used = ++soundCounter;
        return it->second.chunk;
    }
    void freeSoundCache() {
        stopSounds();
        for (auto &c : soundCache)
            Mix_FreeChunk(c.second.chunk);
        soundCache.clear();
        soundCacheBytes = 0;
    }
    void stopSounds() {
        for (auto &f : soundFades)
            f.duration = 0;
        if (audio)
            for (int i = 0; i < 8; i++)
                Mix_HaltChannel(i + 1);
        sounds.fill(nullptr);
    }
    void playSound(int id, bool loop) {
        if (!audio || skipMode || heldSkip)
            return;
        auto p = "se" + number(id) + ".w";
        if (!asset("se.a").contains(p))
            return;
        int ch = soundChannel++ % 8;
        soundFades[ch].duration = 0;
        Mix_Volume(ch + 1,
                   preferences.at("master").get<int>() * preferences.at("sound").get<int>() * 128 / 400);
        Mix_HaltChannel(ch + 1);
        sounds[ch] = nullptr;
        sounds[ch] = soundChunk(id, p);
        soundIds[ch] = id;
        if (sounds[ch])
            Mix_PlayChannel(ch + 1, sounds[ch], loop ? -1 : 0);
    }
    void markMessageRead() {
        if (!messageKey.empty() && page + 1 == int(message.pages.size()) && revealed >= message.length(page))
            uiDirty |= readMessages.insert(messageKey).second;
    }
    void captureDialogueAnchor() {
        dialogueAnchor = {{"runtime", vm->save()},
                          {"visual", visual},
                          {"message", messageEvent},
                          {"messageWaiting", messageWaiting},
                          {"textMode", textMode}};
    }
    void advance() {
        if (page + 1 < int(pages.size())) {
            page++;
            revealed = 0;
            messageTimer = 0;
            autoAt = SDL_GetTicks() + 4000;
            return;
        }
        if (!visual.at("choice").empty())
            return;
        markMessageRead();
        if (visual.at("clear").get<bool>()) {
            visual["text"] = "";
            paginate();
        }
        deadline = 0;
        Json e;
        for (int guard = 0; guard < 10000; guard++) {
            if (!introSeen && !replayMode && vm->introComplete()) {
                introSeen = true;
                uiDirty = true;
                persistUi();
                startOpening();
                return;
            }
            if (!vm->next(e)) {
                if (vm->done()) {
                    if (replayMode) {
                        returnTitle();
                        return;
                    }
                    menu = true;
                    inGame = false;
                    notice = "本路线结束";
                    playMusic(24);
                    stopVoice();
                    stopSounds();
                }
                return;
            }
            int kind = e.at("kind"), v = e.at("value"), s = e.at("slot"), flags = e.at("flags");
            switch (kind) {
            case Background:
                visual["background"] = v;
                if (!replayMode && (v & 0x7fff) >= 0x1000)
                    uiDirty |= unlockedImages.insert(lower(backgroundName(v))).second;
                if (!(flags & 1)) {
                    visual["characters"] = {0, 0, 0};
                    visual["previous"] = {0, 0, 0};
                    visual["count"] = 0;
                }
                break;
            case Speaker:
                visual["speaker"] = v;
                break;
            case Music:
                visual["music"] = v;
                if (!replayMode && v > 0)
                    uiDirty |= unlockedMusic.insert(v).second;
                playMusic(v, s);
                break;
            case ClearCharacters:
                visual["previous"] = visual["characters"];
                visual["characters"] = {0, 0, 0};
                visual["count"] = v;
                break;
            case Character: {
                if (!v)
                    break;
                if (s < 0) {
                    for (int i = 0; i < 3; i++) {
                        int prev = visual["characters"][i];
                        if (prev && (prev & 0xf00) == (v & 0xf00)) {
                            s = i;
                            break;
                        }
                    }
                }
                if (s < 0)
                    break;
                if (s >= 3)
                    throw std::runtime_error("Too many character layers");
                if (!(v & 15)) {
                    int prev = visual["characters"][s];
                    if (!prev)
                        prev = visual["previous"][s];
                    v |= prev & 15;
                }
                visual["characters"][s] = v;
                break;
            }
            case HideCharacter: {
                if (!v) {
                    visual["previous"] = visual["characters"];
                    visual["characters"] = {0, 0, 0};
                    visual["count"] = 0;
                    break;
                }
                Json kept = Json::array();
                for (auto c : visual["characters"])
                    if (c.get<int>() && (c.get<int>() >> 8) != v)
                        kept.push_back(c);
                visual["count"] = kept.size();
                while (kept.size() < 3)
                    kept.push_back(0);
                visual["characters"] = kept;
                break;
            }
            case MessageLayout:
                visual["full"] = v != 0;
                visual["text"] = "";
                visual["speaker"] = 255;
                visual["window"] = v != 0;
                paginate();
                if (beginEffect(3))
                    return;
                break;
            case MessageWindow:
                visual["window"] = v != 0;
                if (!v)
                    visual["speaker"] = 255;
                break;
            case ClearMessage:
                visual["text"] = "";
                paginate();
                break;
            case Dialogue: {
                bool append = !visual.at("text").get<std::string>().empty();
                int oldPage = page;
                float oldReveal = revealed;
                visual["window"] = true;
                std::string add = e.at("text");
                if (!append)
                    while (!add.empty() && add.front() == '\x7f')
                        add.erase(0, 1);
                visual["text"] = visual.at("text").get<std::string>() + add;
                visual["clear"] = (flags & 2) != 0;
                visual["voice"] = e.at("voice");
                paginate();
                if (append) {
                    page = std::min(oldPage, int(message.pages.size()) - 1);
                    revealed = std::min(oldReveal, message.length(page));
                }
                messageWaiting = flags & 1;
                messageEvent = e;
                messageTimer = 0;
                messageKey = e.at("source").get<std::string>() + ":" + std::to_string(v);
                messageRead = readMessages.count(messageKey);
                if (preferences.value("readSkip", false) && messageRead)
                    skipMode = true;
                if (!preferences.value("skipUnread", false) && !messageRead)
                    skipMode = false;
                if (textMode == 0 || preferences.value("instant", false) || skipMode)
                    revealed = message.length(page);
                if (preferences.value("interruptVoice", true) || !e.at("voice").get<std::string>().empty())
                    stopVoice();
                if (!skipMode && !e.at("voice").get<std::string>().empty())
                    playVoice(e.at("voice"));
                if (append && !historyScenes.empty())
                    historyScenes.back() = visual;
                else
                    historyScenes.push_back(visual);
                if (historyScenes.size() > 100)
                    historyScenes.erase(historyScenes.begin());
                captureDialogueAnchor();
                return;
            }
            case WaitMessage:
                visual["clear"] = v != 0;
                messageWaiting = true;
                messageTimer = 0;
                return;
            case Choice:
                visual["choice"] = e.at("options");
                choice = 0;
                sceneKey.clear();
                autoMode = skipMode = false;
                stopVoice();
                return;
            case Pause:
            case PauseFrames:
                deadline = SDL_GetTicks() +
                           uint32_t(kind == Pause ? (v ? v : 10) * 100 : ((v ? v : 90) + 1) * 1000 / 60);
                sceneKey.clear();
                return;
            case Sound:
                playSound(v, s != 0);
                break;
            case StopSound:
                if (audio)
                    for (int i = 0; i < 8; i++)
                        if (soundIds[i] == v) {
                            soundFades[i].duration = 0;
                            if (s < 0 || (!s && !flags))
                                Mix_HaltChannel(i + 1);
                            else {
                                int target = preferences.at("master").get<int>() *
                                             preferences.at("sound").get<int>() * 128 / 400 *
                                             std::clamp(flags, 0, 256) / 256;
                                if (!s)
                                    Mix_Volume(i + 1, target);
                                else
                                    soundFades[i] = {Mix_Volume(i + 1, -1), target, SDL_GetTicks(),
                                                     uint32_t(s)};
                            }
                        }
                break;
            case Weather:
                weather = v;
                visual["weather"] = v;
                snowfall.start(v);
                break;
            case Transition:
                if (v == 40 || v == 42 || v == 43) {
                    tone = v == 40 ? 0 : v;
                    visual["tone"] = tone;
                    if (s)
                        break;
                    v = 2;
                }
                if (beginEffect(v, s))
                    return;
                break;
            case ScreenEffect:
                if (beginEffect(v))
                    return;
                break;
            case Ending:
                endingRoute = std::clamp(v, 1, 5);
                endingCanSkip = clearedEndings.count(endingRoute);
                uiDirty |= clearedEndings.insert(endingRoute).second;
                persistUi();
                prepareEnding();
                endingTextures.clear();
                ending = true;
                endingAt = SDL_GetTicks();
                stopVoice();
                stopSounds();
                playMusic(2, -1, false);
                return;
            case TextMode:
                textMode = v;
                break;
            case UnlockMemoir:
                if (!replayMode)
                    uiDirty |= unlockedMemoirs.insert(v * 32 + s).second;
                break;
            default:
                throw std::runtime_error("Unhandled scene event");
            }
        }
        throw std::runtime_error("Event budget exceeded");
    }
    std::string savePath() const {
        return stateRoot + "/save" + std::to_string(slot) + ".json";
    }
    void saveGame() {
        if (!inGame || ending || dialogueAnchor.is_null()) {
            notice = "请在剧情画面保存";
            return;
        }
        Json save = dialogueAnchor;
        save["format"] = 1;
        save["messageStateVersion"] = 1;
        save["page"] = page;
        save["surname"] = surname;
        save["given"] = given;
        std::time_t now = std::time(nullptr);
        char stamp[64];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M", std::localtime(&now));
        save["savedAt"] = stamp;
        save["history"] = historyScenes;
        save["pendingAdvance"] = false;
        auto p = savePath(), tmp = p + ".tmp", backup = p + ".bak";
        {
            std::ofstream f(tmp, std::ios::binary);
            if (!f)
                throw std::runtime_error("Cannot write save");
            f << save.dump();
            f.flush();
            if (!f)
                throw std::runtime_error("Save write failed");
        }
        std::remove(backup.c_str());
        if (exists(p) && std::rename(p.c_str(), backup.c_str()))
            throw std::runtime_error("Cannot back up save");
        if (std::rename(tmp.c_str(), p.c_str())) {
            std::rename(backup.c_str(), p.c_str());
            throw std::runtime_error("Cannot commit save");
        }
        haveSaveSlots = true;
        persistUi();
        notice.clear();
    }
    void loadGame() {
        auto p = savePath();
        if (!exists(p)) {
            notice = "该槽位没有存档";
            return;
        }
        try {
            auto data = readJson(p);
            if (data.at("format") != 1)
                throw std::runtime_error("Unknown save format");
            auto candidate = std::make_unique<Runtime>(root);
            candidate->load(data.at("runtime"));
            Json nextVisual = data.at("visual");
            // Validate the candidate before replacing the currently running game.
            validateSaveVisual(nextVisual);
            Json restoredMessage = data.value("message", Json());
            if (restoredMessage.is_null())
                restoredMessage = candidate->savedDialogue(nextVisual.at("text"), nextVisual.at("voice"));
            std::string restoredKey;
            if (!restoredMessage.is_null()) {
                restoredKey = restoredMessage.at("source").get<std::string>() + ":" +
                              std::to_string(restoredMessage.at("value").get<int>());
                restoredMessage.at("text").get<std::string>();
            }
            auto restoredHistory = data.value("history", std::vector<Json>{});
            auto restoredSurname = data.value("surname", std::string("木田"));
            auto restoredGiven = data.value("given", std::string("时纪"));
            int restoredPage = data.value("page", 0), restoredTextMode = data.value("textMode", 1);
            bool restoredWaiting = data.value("messageWaiting", true);
            bool restoredPending = data.value("pendingAdvance", false);
            int restoredTone = nextVisual.value("tone", 0), restoredWeather = nextVisual.value("weather", 0);
            for (const auto &item : nextVisual.at("choice"))
                item.get<std::string>();
            for (const auto &entry : restoredHistory) {
                for (const char *key : {"background", "count", "speaker"})
                    entry.at(key).get<int>();
                for (const char *key : {"text", "voice"})
                    entry.at(key).get<std::string>();
                for (const char *key : {"full", "window"})
                    entry.at(key).get<bool>();
                if (entry.at("characters").size() != 3)
                    throw std::runtime_error("Invalid history characters");
                for (const auto &character : entry.at("characters"))
                    character.get<int>();
            }
            if (restoredHistory.size() > 100)
                restoredHistory.erase(restoredHistory.begin(), restoredHistory.end() - 100);
            ending = false;
            endingTextures.clear();
            vm = std::move(candidate);
            visual = data.at("visual");
            surname = restoredSurname;
            given = restoredGiven;
            effectId = 0;
            replayMode = data.at("runtime").value("replay", false);
            autoMode = skipMode = false;
            hidden = false;
            showUi(UiNone);
            historyScenes = std::move(restoredHistory);
            messageWaiting = restoredWaiting;
            textMode = restoredTextMode;
            messageEvent = std::move(restoredMessage);
            messageKey = restoredKey;
            messageRead = readMessages.count(messageKey);
            skipMode = preferences.value("readSkip", false) && messageRead;
            messageTimer = skipTimer = 0;
            tone = restoredTone;
            weather = restoredWeather;
            snowfall.start(weather);
            sceneKey.clear();
            history.clear();
            choice = 0;
            stopSounds();
            sceneKey.clear();
            paginate();
            page = std::clamp(restoredPage, 0, std::max(0, int(pages.size()) - 1));
            revealed = 0;
            stopMusic();
            playMusic(visual.at("music"));
            stopVoice();
            if (!skipMode)
                playVoice(visual.at("voice"));
            menu = false;
            inGame = true;
            deadline = 0;
            autoAt = SDL_GetTicks() + 4000;
            if (restoredPending)
                deadline = SDL_GetTicks() + 1;
            if (vm->introComplete()) {
                introSeen = true;
                uiDirty = true;
            }
            captureDialogueAnchor();
            notice = "";
        } catch (const std::exception &error) {
            notice = "存档无法读取，请选择其他槽位";
            fprintf(stderr, "Save load rejected: %s\n", error.what());
        }
    }
    void resetStory() {
        showUi(UiNone);
        autoMode = skipMode = false;
        skipTimer = 0;
        messageEvent = dialogueAnchor = nullptr;
        effectId = 0;
        hidden = false;
        replayMode = false;
        textMode = 1;
        messageKey.clear();
        historyScenes.clear();
        sceneKey.clear();
        snowfall.start(0);
        ending = false;
        endingTextures.clear();
        vm = std::make_unique<Runtime>(root);
        visual = {{"background", -1},
                  {"characters", {0, 0, 0}},
                  {"previous", {0, 0, 0}},
                  {"count", 0},
                  {"speaker", 255},
                  {"text", ""},
                  {"full", false},
                  {"window", true},
                  {"clear", true},
                  {"choice", Json::array()},
                  {"music", 0},
                  {"voice", ""},
                  {"weather", 0},
                  {"tone", 0}};
        weather = tone = 0;
        history.clear();
        pages.clear();
        page = 0;
        inGame = true;
        menu = false;
        deadline = 0;
        stopVoice();
        stopSounds();
        stopMusic();
        paginate();
    }
    void newGame() {
        resetStory();
        if (introSeen)
            vm->skipPrologue();
        advance();
    }
    void action(int a) {
        originalAction(a);
    }
    Texture &endingTexture(int key) {
        if (!endingTextures.count(key)) {
            if (endingTextures.size() > 16)
                endingTextures.clear();
            auto t = std::make_unique<Texture>();
            t->load(r, key >= 1000 ? endingStrip.rows((key - 1000) * 512,
                                                      std::min(512, endingStrip.h - (key - 1000) * 512))
                                   : pxFrame(endingCredits, key));
            endingTextures[key] = std::move(t);
        }
        return *endingTextures.at(key);
    }
    void drawEnding() {
        double t = (SDL_GetTicks() - endingAt) / 1000.0;
        rectangle(0, 0, 800, 600, {0, 0, 0, 255});
        if (t >= 9 && t < 14.78)
            rectangle(0, 0, 800, 600, {255, 255, 255, uint8_t(std::clamp((t - 9) / 4.28, 0., 1.) * 255)});
        if (t >= 14.78 && t < 22.03) {
            endingTexture(1).draw(r, 0, 288);
            rectangle(0, 0, 800, 600,
                      {255, 255, 255, uint8_t(std::clamp(1 - (t - 14.78) / 4.25, 0., 1.) * 255)});
        }
        if (t < 13.28) {
            std::string intro;
            auto &ls = endingInfo.at("lines");
            float distance = 0;
            for (int i = 0; i < 3 && i < int(ls.size()) && t >= i * 3; i++) {
                auto part = formatted(ls[i]);
                NativeMessageLayout one;
                one.set(part, true);
                float length = 0;
                for (size_t j = 0; j < one.pages.size(); j++)
                    length += one.length(int(j));
                distance += std::min(length, float((t - i * 3) * 660));
                intro += part;
            }
            NativeMessageLayout layout;
            layout.set(intro, true);
            glyphOpacity = float(std::clamp((13.28 - t) / 4.28, 0., 1.));
            drawMessage(layout, 0, distance, 74, 20, preferences.value("smoothFull", true));
            glyphOpacity = 1;
        }
        if (t >= 22.03) {
            double scroll = std::min(5340., (t - 22.03) * 20);
            int ornament = int(std::fmod(-(t - 22.03) * 180, 50));
            for (int i = 0; i < 3; i++) {
                endingTexture(0).draw(r, 20, ornament + i * 250);
                endingTexture(0).draw(r, 380, ornament + i * 250);
            }
            int height = endingStrip.h;
            for (int tile = std::max(0, int(scroll) / 512); tile * 512 < height && tile * 512 < scroll + 600;
                 tile++)
                endingTexture(1000 + tile).draw(r, 60, tile * 512 - int(scroll));
            int groups[] = {2,  4,  6,  9,  11, 13, 19, 21, 24, 29, 31,
                            33, 39, 45, 50, 54, 56, 59, 68, 79, 81, 83};
            int counts[] = {2, 2, 3, 2, 2, 6, 2, 3, 5, 2, 2, 6, 6, 5, 4, 2, 3, 9, 11, 2, 2, 2};
            double remaining = t - 25.646;
            for (int group = 0; group < 22 && remaining > 0; group++) {
                double duration = 2.126 + 3.5 + (counts[group] - 2) * 1.1 + 2.066 + 1;
                if (remaining > duration) {
                    remaining -= duration;
                    continue;
                }
                int y = (600 - (24 + 24 * counts[group])) / 2;
                for (int row = 0; row < counts[group]; row++) {
                    int id = groups[group] + row;
                    float alpha = float(
                        std::clamp((remaining - 2.126 - (row == 0 || group == 17 ? 0 : 55. / 60)) * 60 / 128,
                                   0., 1.) *
                        std::clamp(duration - remaining, 0., 1.));
                    auto &texture = endingTexture(id);
                    SDL_SetTextureAlphaMod(texture.ptr, uint8_t(alpha * 255));
                    texture.draw(r, 420, y);
                    SDL_SetTextureAlphaMod(texture.ptr, 255);
                    y += 24;
                    if (group == 17 && (row == 1 || row == 4))
                        y += 24;
                }
                break;
            }
        }
        if (t >= 22.03 && t < 24.146) {
            auto &texture = endingTexture(1);
            SDL_SetTextureAlphaMod(texture.ptr, uint8_t(std::clamp(1 - (t - 22.03) / 2.116, 0., 1.) * 255));
            texture.draw(r, 0, 288);
            SDL_SetTextureAlphaMod(texture.ptr, 255);
        }
        if (t > 258.75)
            rectangle(0, 0, 800, 600, {0, 0, 0, uint8_t(std::clamp((t - 258.75) / 4.25, 0., 1.) * 255)});
    }
    void draw() {
        drawPresentation();
    }

  public:
    explicit App(std::string path, std::string state) : root(std::move(path)), stateRoot(std::move(state)) {}
    ~App() {
        stopVoiceWorker();
        movie.stop();
        snapshots.discard();
        snapshots.last = FrameSnapshot();
        uiCache.clear();
        glyphCache.clear();
        previews.clear();
        effectCover.reset();
        sceneComposite.reset();
        canvas.reset();
        logo.reset();
        nameBackground.reset();
        confirmShade.reset();
        for (auto &f : sizedFonts)
            TTF_CloseFont(f.second);
        for (auto &f : alternateFonts)
            TTF_CloseFont(f.second);
        textCache.clear();
        endingTextures.clear();
        stopVoice();
        freeSoundCache();
        stopMusic();
        musicStream = nullptr;
        if (music)
            Mix_FreeMusic(music);
        if (audio)
            Mix_CloseAudio();
        title.reset();
        if (font)
            TTF_CloseFont(font);
        if (pad)
            SDL_GameControllerClose(pad);
        if (r)
            SDL_DestroyRenderer(r);
        if (window)
            SDL_DestroyWindow(window);
        Mix_Quit();
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
    }
    int run(int smoke = 0, int testMode = 0) {
        std::setvbuf(stderr, nullptr, _IONBF, 0);
        SDL_SetMainReady();
        SDL_setenv("VITA_DISABLE_TOUCH_BACK", "1", 1);
        // Receive front-panel touches directly; one touch must activate only once.
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#ifdef SDL_HINT_VITA_TOUCH_MOUSE_DEVICE
        SDL_SetHint(SDL_HINT_VITA_TOUCH_MOUSE_DEVICE, "0");
#endif
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER))
            throw std::runtime_error(SDL_GetError());
        window = SDL_CreateWindow("Tenshi Vita", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 544,
                                  SDL_WINDOW_SHOWN);
        if (!window)
            throw std::runtime_error(SDL_GetError());
        r = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!r)
            r = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!r)
            throw std::runtime_error(SDL_GetError());
        SDL_RenderSetLogicalSize(r, 800, 600);
        canvas.ptr = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 800, 600);
        canvas.w = 800;
        canvas.h = 600;
        if (!canvas.ptr)
            throw std::runtime_error(SDL_GetError());
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        IMG_Init(IMG_INIT_PNG);
        const int ttfResult = TTF_Init();
        Mix_Init(MIX_INIT_OGG);
        audio = Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 2048) == 0;
        if (audio) {
            Mix_QuerySpec(&audioFrequency, &audioFormat, &audioChannels);
            Mix_AllocateChannels(10);
            Mix_Volume(-1, volume);
            Mix_VolumeMusic(volume);
        }
        for (int i = 0; i < SDL_NumJoysticks(); i++)
            if (SDL_IsGameController(i)) {
                pad = SDL_GameControllerOpen(i);
                break;
            }
        try {
            if (ttfResult)
                throw std::runtime_error(std::string("FONT ENGINE INIT FAILED: ") + TTF_GetError());
            vm = std::make_unique<Runtime>(root);
            for (const char *name : {"egbg.a", "char.a", "music.a", "voice.a", "se.a", "sys.a", "ed.a"})
                asset(name);
            if (!exists(openingPath()))
                throw std::runtime_error("MISSING OPENING VIDEO: " + openingPath());
            font = openFont("font.ttf", 24);
            fprintf(stderr, "Font loaded: %s\n", fontPath("font.ttf").c_str());
            if (!audio)
                throw std::runtime_error("AUDIO INITIALIZATION FAILED.");
            title.load(r, pxFrame(lzss(asset("sys.a").read("title.px")), 5));
            logo.load(r, pxFrame(atlas("leaflogo.px"), 0), pxFrame(atlas("leaflogo.px"), 1));
            if (exists(stateRoot + "/player.json")) {
                auto p = readJson(stateRoot + "/player.json");
                surname = p.value("surname", surname);
                given = p.value("given", given);
            }
            loadUi();
            playMusic(24);
        } catch (const std::exception &e) {
            fatal = e.what();
            fprintf(stderr, "%s\n", fatal.c_str());
        }
        if (smoke && !fatal.empty())
            return 1;
        if (smoke && testMode == 4) {
            verifyUi();
            return 0;
        }
        if (smoke && testMode == 5) {
            verifyEffects();
            return 0;
        }
        if (smoke && testMode == 6) {
            verifyInput();
            return 0;
        }
        if (smoke && testMode == 7) {
            verifyStartup();
            return 0;
        }
        if (smoke && testMode == 8) {
            verifyRestart();
            return 0;
        }
        if (!smoke && fatal.empty()) {
            splash = true;
            splashAt = SDL_GetTicks();
            logoDirections = snowfall.rand(8);
            logoVariant = snowfall.rand(8);
            playMusic(0);
        }
        if (smoke) {
            introSeen = true;
            if (testMode == 1) {
                movie.start(r, openingPath());
                menu = false;
            } else if (testMode == 2) {
                endingRoute = 1;
                prepareEnding();
                ending = true;
                endingAt = SDL_GetTicks() - 60000;
                menu = false;
            } else if (testMode == 0)
                newGame();
        }
        int frames = 0;
        uint32_t movieLogAt = 0, logoLogAt = 0, logoLogDraws = 0;
        uint64_t logoLogTicks = 0;
        draw();
        while (running) {
            const uint32_t frameStart = SDL_GetTicks();
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                try {
                    if (e.type == SDL_QUIT)
                        running = false;
                    if (e.type == SDL_TEXTINPUT && uiScreen == UiName) {
                        inputName(e.text.text, false);
                        continue;
                    }
                    if (e.type == SDL_KEYDOWN && uiScreen == UiName && nameField >= 0 &&
                        e.key.keysym.sym == SDLK_BACKSPACE) {
                        inputName("", true);
                        continue;
                    }
                    if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                        switch (e.key.keysym.sym) {
                        case SDLK_RETURN:
                        case SDLK_SPACE:
                            action(1);
                            break;
                        case SDLK_ESCAPE:
                            action(6);
                            break;
                        case SDLK_BACKSPACE:
                            action(2);
                            break;
                        case SDLK_UP:
                        case SDLK_PAGEUP:
                            if (uiScreen == UiNone && inGame && visual.at("choice").empty())
                                action(5);
                            else
                                action(3);
                            break;
                        case SDLK_DOWN:
                        case SDLK_PAGEDOWN:
                            action(4);
                            break;
                        case SDLK_LEFT:
                            action(7);
                            break;
                        case SDLK_RIGHT:
                            action(8);
                            break;
                        case SDLK_a:
                            if (uiScreen == UiNone)
                                action(10);
                            break;
                        case SDLK_s:
                            if (uiScreen == UiNone)
                                action(9);
                            break;
                        case SDLK_h:
                            if (uiScreen == UiNone)
                                action(2);
                            break;
                        case SDLK_v:
                            if (uiScreen == UiNone && inGame &&
                                !visual.at("voice").get<std::string>().empty()) {
                                playVoice(visual.at("voice"));
                                messageTimer = 0;
                            }
                            break;
                        case SDLK_F5:
                            if (uiScreen == UiNone && inGame)
                                dispatch(3, 0);
                            break;
                        case SDLK_F9:
                            if (uiScreen == UiNone && inGame)
                                dispatch(3, 1);
                            break;
                        default:
                            break;
                        }
                    }
                    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                        switch (e.cbutton.button) {
                        case SDL_CONTROLLER_BUTTON_A:
                            action(1);
                            break;
                        case SDL_CONTROLLER_BUTTON_B:
                            action(2);
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_UP:
                            action(3);
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                            action(4);
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                            action(7);
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                            action(8);
                            break;
                        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                            action(9);
                            break;
                        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                            action(10);
                            break;
                        case SDL_CONTROLLER_BUTTON_Y:
                            action(5);
                            break;
                        case SDL_CONTROLLER_BUTTON_START:
                            action(6);
                            break;
                        default:
                            break;
                        }
                    }
                    if (pointerEvent(e))
                        continue;
                    if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                        touchPressed = pointerHeld = false;
                        pressedAction = draggedSlider = -1;
                    }
                    if (e.type == SDL_APP_WILLENTERBACKGROUND)
                        setSuspended(true);
                    if (e.type == SDL_APP_DIDENTERFOREGROUND)
                        setSuspended(false);
                } catch (const std::exception &err) {
                    snapshots.discard();
                    movie.stop();
                    fatal = err.what();
                    fprintf(stderr, "%s\n", fatal.c_str());
                }
            }
            if (suspended) {
                SDL_Delay(20);
                continue;
            }
            try {
                heldSkip = (SDL_GetModState() & KMOD_CTRL) != 0;
                pollVoice();
                pollMusicStream();
                tickPresentation();
                if (smoke && testMode == 0 && ++frames % 2 == 0) {
                    if (ending) {
                        ending = false;
                        endingTextures.clear();
                    }
                    if (!visual.at("choice").empty()) {
                        vm->choose(1);
                        visual["choice"] = Json::array();
                    }
                    effectId = 0;
                    deadline = 0;
                    revealed = message.length(page);
                    advance();
                    if (frames >= smoke) {
                        saveGame();
                        loadGame();
                        fprintf(stderr, "Story save/load smoke passed: %d frames\n", frames);
                        running = false;
                    }
                }
                if (smoke && testMode != 0 && ++frames >= smoke) {
                    fprintf(stderr, "Presentation smoke mode %d passed: %d frames\n", testMode, frames);
                    running = false;
                }
                draw();
            } catch (const std::exception &err) {
                snapshots.discard();
                movie.stop();
                fatal = err.what();
                fprintf(stderr, "%s\n", fatal.c_str());
                if (smoke)
                    return 1;
            }
            if (splash && fatal.empty()) {
                if (!logoLogAt) {
                    logoLogAt = SDL_GetTicks();
                    logoLogDraws = logo.draws;
                    logoLogTicks = logo.submitTicks;
                } else if (SDL_GetTicks() - logoLogAt >= 1000 && perfLines < 80) {
                    uint32_t now = SDL_GetTicks(), count = logo.draws - logoLogDraws;
                    double tickMs = 1000. / SDL_GetPerformanceFrequency();
                    fprintf(stderr,
                            "Perf: logo %.1f fps, submit avg %.3f ms, peak %.3f ms, initial upload %u bytes, "
                            "per-frame upload 0\n",
                            count * 1000. / (now - logoLogAt),
                            count ? (logo.submitTicks - logoLogTicks) * tickMs / count : 0.,
                            logo.peakTicks * tickMs, unsigned(logo.uploadBytes));
                    perfLines++;
                    logoLogAt = now;
                    logoLogDraws = logo.draws;
                    logoLogTicks = logo.submitTicks;
                    logo.peakTicks = 0;
                }
            }
            if (movie.active()) {
                // The opening reports its own throughput instead of per-frame lines.
                if (!movieLogAt)
                    movieLogAt = SDL_GetTicks();
                else if (SDL_GetTicks() - movieLogAt >= 5000 && perfLines < 80) {
                    perfLines++;
                    fprintf(stderr,
                            "Perf: opening %u loops, %u new frames, decode+upload avg %u ms, %d B dropped\n",
                            movie.draws, movie.uploads, movie.draws ? movie.decodeMs / movie.draws : 0,
                            movie.skippedFrames());
                    movie.draws = movie.uploads = movie.decodeMs = 0;
                    movieLogAt = SDL_GetTicks();
                }
            } else {
                movieLogAt = 0;
                if (SDL_GetTicks() - frameStart >= 100)
                    perfLog("slow frame", SDL_GetTicks() - frameStart);
            }
            // With vsync the present already paces the loop. The old unconditional 16 ms sleep on top
            // of it made every frame miss a vblank, capping the Vita at 30 fps or less.
            SDL_RendererInfo info{};
            if (SDL_GetRendererInfo(r, &info) || !(info.flags & SDL_RENDERER_PRESENTVSYNC)) {
                uint32_t spent = SDL_GetTicks() - frameStart;
                if (spent < 16)
                    SDL_Delay(16 - spent);
            }
        }
        if (fatal.empty())
            persistUi();
        return fatal.empty() ? 0 : 1;
    }
};
int main(int argc, char **argv) {
#ifdef __vita__
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    int logDirStatus = sceIoMkdir("ux0:/data/tenshi", 0777);
    FILE *logFile = std::freopen("ux0:/data/tenshi/runtime.log", "w", stderr);
    if (!logFile)
        return 1;
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    fprintf(stderr, "Tenshi Vita 0.27: log directory result=0x%x\n", unsigned(logDirStatus));
    const std::string root = "ux0:/data/tenshi";
    const std::string stateRoot = root;
    int smoke = 0, testMode = 0;
    (void)argc;
    (void)argv;
#else
    const std::string root = argc > 1 ? argv[1] : "data";
    const std::string stateRoot = argc > 4 ? argv[4] : root;
    int smoke = argc > 3 ? std::stoi(argv[3]) : 0;
    int testMode = argc > 2 && std::string(argv[2]) == "--movie-smoke"     ? 1
                   : argc > 2 && std::string(argv[2]) == "--ending-smoke"  ? 2
                   : argc > 2 && std::string(argv[2]) == "--menu-smoke"    ? 3
                   : argc > 2 && std::string(argv[2]) == "--ui-smoke"      ? 4
                   : argc > 2 && std::string(argv[2]) == "--effects-smoke" ? 5
                   : argc > 2 && std::string(argv[2]) == "--input-smoke"   ? 6
                   : argc > 2 && std::string(argv[2]) == "--startup-smoke" ? 7
                   : argc > 2 && std::string(argv[2]) == "--restart-smoke" ? 8
                                                                           : 0;
#endif
    try {
        // App is ~183 KiB (mostly the movie audio ring). Keep it off the Vita main thread's
        // 256 KiB stack so deep FreeType/CFF text rendering still has headroom.
        auto app = std::make_unique<App>(root, stateRoot);
        return app->run(smoke, testMode);
    } catch (const std::exception &e) {
        fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
