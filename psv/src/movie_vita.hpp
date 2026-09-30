#pragma once
#include "movie_index.hpp"
#include "vita_hw_codec.hpp"
#include <array>
// The fixed MP4 is decoded directly through SceVideodec/SceAudiodec. SDL owns
// presentation and audio output; its callback never enters a decoder or reads a file.
class Movie {
    MovieIndex index;
    VitaHardwareCodec codec;
    SDL_Texture *texture = nullptr;
    SDL_mutex *mutex = nullptr;
    bool opened = false, paused = false, hooked = false, pending = false, videoEnded = false;
    uint32_t started = 0, progressAt = 0, pauseAt = 0;
    size_t nextVideo = 0, nextAudio = 0;
    uint32_t decoded = 0, dropped = 0;
    uint64_t pendingPts = 0, queuedSamples = 0, playedSamples = 0;
    std::array<Uint8, 44100 * 4> ring{};
    size_t head = 0, tail = 0, used = 0;
    std::vector<uint8_t> videoPacket, audioPacket;
    bool packetLoaded = false;
    static void mix(void *user, Uint8 *out, int bytes) {
        auto &m = *static_cast<Movie *>(user);
        SDL_memset(out, 0, bytes);
        SDL_LockMutex(m.mutex);
        if (!m.paused) {
            size_t n = std::min(size_t(bytes), m.used), first = std::min(n, m.ring.size() - m.head);
            memcpy(out, m.ring.data() + m.head, first);
            memcpy(out + first, m.ring.data(), n - first);
            m.head = (m.head + n) % m.ring.size();
            m.used -= n;
            m.playedSamples += n / 4;
        }
        SDL_UnlockMutex(m.mutex);
    }
    uint64_t clockSamples() const {
        if (!mutex)
            return 0;
        SDL_LockMutex(mutex);
        auto value = playedSamples;
        SDL_UnlockMutex(mutex);
        return value;
    }
    void pumpAudio() {
        for (int step = 0; step < 16 && nextAudio < index.audio.size(); ++step) {
            SDL_LockMutex(mutex);
            bool enough = used >= 8 * 1024 * 4;
            SDL_UnlockMutex(mutex);
            if (enough)
                break;
            const auto &packet = index.audio[nextAudio];
            index.read(packet, audioPacket);
            uint32_t samples = codec.decodeAudio(audioPacket);
            if (samples != 1024)
                throw std::runtime_error("HW AAC expected 1024 PCM samples per access unit");
            int64_t from = std::max(int64_t(0), packet.pts),
                    to = std::min(int64_t(index.audioSamples), packet.pts + samples);
            if (to > from) {
                size_t bytes = size_t(to - from) * 4, offset = size_t(from - packet.pts) * 4;
                SDL_LockMutex(mutex);
                size_t first = std::min(bytes, ring.size() - tail);
                memcpy(ring.data() + tail, codec.pcm() + offset, first);
                memcpy(ring.data(), codec.pcm() + offset + first, bytes - first);
                tail = (tail + bytes) % ring.size();
                used += bytes;
                SDL_UnlockMutex(mutex);
                queuedSamples += uint64_t(to - from);
                if (queuedSamples == uint64_t(to - from))
                    fprintf(stderr, "HW first audio: 44100 Hz stereo, AAC priming trimmed\n");
            }
            ++nextAudio;
        }
        if (nextAudio == index.audio.size() && queuedSamples != index.audioSamples)
            throw std::runtime_error("HW AAC output duration mismatch");
    }
    void fetchVideo() {
        if (pending || videoEnded)
            return;
        const uint32_t begin = SDL_GetTicks();
        for (int step = 0; step < 8 && SDL_GetTicks() - begin < 20; ++step) {
            bool input = nextVideo < index.video.size();
            if (input && !packetLoaded) {
                index.read(index.video[nextVideo], videoPacket, true, nextVideo == 0);
                packetLoaded = true;
            }
            bool accepted = false, ended = false;
            uint64_t nativePts = 0;
            bool picture = codec.decodeVideo(input ? &videoPacket : nullptr,
                                             input ? uint64_t(index.video[nextVideo].pts) : 0, accepted,
                                             ended, nativePts);
            if (accepted) {
                ++nextVideo;
                if (nextVideo <= 3)
                    fprintf(stderr, "HW AVC AU %u: bytes=%u picture=%d\n", unsigned(nextVideo),
                            unsigned(videoPacket.size()), int(picture));
                packetLoaded = false;
            }
            if (picture) {
                // The indexed clip is fixed 30 fps without B frames; all AUs are decoded.
                // The ordinal is an exact presentation clock even when native PTS is absent.
                pendingPts = uint64_t(decoded) * 3000;
                if (++decoded > index.video.size())
                    throw std::runtime_error("HW AVC produced too many frames");
                pending = true;
                progressAt = SDL_GetTicks();
                if (decoded == 1)
                    fprintf(stderr, "HW first video: %ux%u NV12, native pts=%llu, input AUs=%u\n",
                            codec.stride(), codec.rows(), static_cast<unsigned long long>(nativePts),
                            unsigned(nextVideo));
                return;
            }
            if (ended) {
                videoEnded = true;
                fprintf(stderr, "HW AVC drained: %u/%u frames\n", decoded, unsigned(index.video.size()));
                if (decoded != index.video.size())
                    throw std::runtime_error("HW AVC ended with missing frames");
                return;
            }
            if (!accepted)
                break;
        }
    }

  public:
    uint32_t decodeMs = 0, uploads = 0, draws = 0;
    ~Movie() {
        stop();
    }
    bool active() const {
        return opened;
    }
    bool isPaused() const {
        return paused;
    }
    int skippedFrames() const {
        return int(dropped);
    }
    const char *backend() const {
        return "SceVideodec direct AVC + SceAudiodec AAC / NV12";
    }
    double position() const {
        return clockSamples() / 44100.;
    }
    void setPaused(bool value) {
        if (!opened || paused == value)
            return;
        SDL_LockMutex(mutex);
        paused = value;
        SDL_UnlockMutex(mutex);
        if (value)
            pauseAt = SDL_GetTicks();
        else {
            uint32_t elapsed = SDL_GetTicks() - pauseAt;
            started += elapsed;
            progressAt += elapsed;
        }
    }
    void stop() {
        if (hooked)
            Mix_HookMusic(nullptr, nullptr);
        hooked = false;
        opened = false;
        codec.close();
        index.close();
        if (texture)
            SDL_DestroyTexture(texture);
        texture = nullptr;
        if (mutex)
            SDL_DestroyMutex(mutex);
        mutex = nullptr;
        paused = pending = videoEnded = packetLoaded = false;
        head = tail = used = nextVideo = nextAudio = 0;
        queuedSamples = playedSamples = 0;
        decoded = dropped = decodeMs = uploads = draws = 0;
        videoPacket.clear();
        audioPacket.clear();
    }
    void start(SDL_Renderer *r, const std::string &file) {
        stop();
        try {
            fprintf(stderr, "HW video begin: %s\n", file.c_str());
            index.open(file);
            mutex = SDL_CreateMutex();
            if (!mutex)
                throw std::runtime_error(SDL_GetError());
            codec.open(index.width, index.height, index.refs, index.rate, index.channels);
            texture = SDL_CreateTexture(r, SDL_PIXELFORMAT_NV12, SDL_TEXTUREACCESS_STREAMING, codec.stride(),
                                        codec.rows());
            if (!texture)
                throw std::runtime_error(SDL_GetError());
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
            Mix_HaltMusic();
            Mix_HaltChannel(-1);
            started = progressAt = SDL_GetTicks();
            opened = true;
            fprintf(stderr, "Movie backend: %s; indexed video=%u audio=%u\n", backend(),
                    unsigned(index.video.size()), unsigned(index.audio.size()));
        } catch (...) {
            stop();
            throw;
        }
    }
    bool draw(SDL_Renderer *r) {
        if (!opened)
            return false;
        uint32_t begin = SDL_GetTicks();
        if (!paused) {
            pumpAudio();
            // Decode references even when late; only presentation may drop a frame.
            for (int step = 0; step < 8; ++step) {
                fetchVideo();
                if (!pending)
                    break;
                uint64_t clock = clockSamples() * 90000 / 44100;
                if (hooked && pendingPts > clock + 1500)
                    break;
                if (hooked && pendingPts + 3000 < clock && decoded < index.video.size()) {
                    pending = false;
                    ++dropped;
                    continue;
                }
                if (SDL_UpdateTexture(texture, nullptr, codec.pixels(), codec.stride()))
                    throw std::runtime_error(SDL_GetError());
                pending = false;
                ++uploads;
                if (!hooked) {
                    // First video and prefilled audio share one start gate.
                    Mix_HookMusic(mix, this);
                    hooked = true;
                    fprintf(stderr, "HW playback started: first frame + PCM ready\n");
                }
                break;
            }
            if (!hooked && SDL_GetTicks() - started > 3000) {
                fprintf(stderr,
                        "HW startup stalled: input AUs=%u output=%u AAC packets=%u queued samples=%llu\n",
                        unsigned(nextVideo), decoded, unsigned(nextAudio),
                        static_cast<unsigned long long>(queuedSamples));
                throw std::runtime_error("HW AVC no first frame after submitted access units");
            }
            if (hooked && !videoEnded && !pending && SDL_GetTicks() - progressAt > 3000)
                throw std::runtime_error("HW AVC stopped producing frames");
            decodeMs += SDL_GetTicks() - begin;
        }
        ++draws;
        if (hooked) {
            SDL_Rect src{0, 0, 800, 600}, dst{0, 0, 800, 600};
            if (SDL_RenderCopy(r, texture, &src, &dst))
                throw std::runtime_error(SDL_GetError());
        }
        if (!paused && videoEnded && !pending && clockSamples() >= index.audioSamples) {
            fprintf(stderr, "HW EOF: video=%u, audio samples=%llu, presentation drops=%u\n", decoded,
                    static_cast<unsigned long long>(playedSamples), dropped);
            return false;
        }
        return true;
    }
};
