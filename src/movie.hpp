#pragma once
#ifdef __vita__
#include "movie_vita.hpp"
#else
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
// Streaming MPEG-1/MP2 playback. Audio ring has a fixed allocation; original videos
// are decoded directly from the player-provided openning.v.
// Decoded frames go to the GPU as Y/Cb/Cr planes (IYUV texture, colour conversion on the GPU),
// once per presented frame. While decoding falls behind, B pictures are dropped so audio and
// video stay in sync instead of the whole movie slowing into a slideshow.
class Movie {
    plm_t *decoder = nullptr;
    SDL_Texture *texture = nullptr;
    SDL_mutex *mutex = nullptr;
    std::array<int16_t, 44100 * 2> ring{};
    size_t head = 0, tail = 0, used = 0;
    std::vector<uint8_t> pixels; // RGB fallback when the renderer has no IYUV textures
    plm_frame_t *latest = nullptr;
    bool yuv = false;
    int width = 0, height = 0;
    uint32_t last = 0;
    bool paused = false;

  public:
    // Playback statistics for runtime.log.
    uint32_t decodeMs = 0, uploads = 0, draws = 0;

  private:
    static void mix(void *user, Uint8 *stream, int bytes) {
        auto &m = *static_cast<Movie *>(user);
        SDL_memset(stream, 0, bytes);
        SDL_LockMutex(m.mutex);
        if (m.paused) {
            SDL_UnlockMutex(m.mutex);
            return;
        }
        auto *dst = reinterpret_cast<int16_t *>(stream);
        size_t n = std::min(m.used, size_t(bytes / 2));
        for (size_t i = 0; i < n; i++) {
            dst[i] = m.ring[m.head];
            m.head = (m.head + 1) % m.ring.size();
        }
        m.used -= n;
        SDL_UnlockMutex(m.mutex);
    }
    static void audioFrame(plm_t *, plm_samples_t *s, void *user) {
        auto &m = *static_cast<Movie *>(user);
        SDL_LockMutex(m.mutex);
        for (size_t i = 0; i < s->count * 2 && m.used < m.ring.size(); i++) {
            m.ring[m.tail] = int16_t(std::clamp(s->interleaved[i], -1.f, 1.f) * 32767.f);
            m.tail = (m.tail + 1) % m.ring.size();
            m.used++;
        }
        SDL_UnlockMutex(m.mutex);
    }
    // Only remember the frame; if one plm_decode() call yields several, just the last is uploaded.
    // Its planes stay valid until the next plm_decode() call.
    static void videoFrame(plm_t *, plm_frame_t *f, void *user) {
        static_cast<Movie *>(user)->latest = f;
    }
    void upload() {
        if (!latest)
            return;
        if (yuv)
            SDL_UpdateYUVTexture(texture, nullptr, latest->y.data, int(latest->y.width), latest->cb.data,
                                 int(latest->cb.width), latest->cr.data, int(latest->cr.width));
        else {
            plm_frame_to_rgb(latest, pixels.data(), width * 3);
            SDL_UpdateTexture(texture, nullptr, pixels.data(), width * 3);
        }
        latest = nullptr;
        uploads++;
    }

  public:
    ~Movie() {
        stop();
    }
    void setPaused(bool value) {
        if (!mutex)
            return;
        SDL_LockMutex(mutex);
        paused = value;
        SDL_UnlockMutex(mutex);
        last = SDL_GetTicks();
    }
    double position() const {
        return decoder ? plm_get_time(decoder) : 0;
    }
    bool isPaused() const {
        return paused;
    }
    bool active() const {
        return decoder != nullptr;
    }
    const char *backend() const {
        return "pl_mpeg software/host reference";
    }
    int skippedFrames() const {
        return decoder && decoder->video_decoder ? plm_video_get_frames_skipped(decoder->video_decoder) : 0;
    }
    void stop() {
        if (decoder) {
            Mix_HookMusic(nullptr, nullptr);
            plm_destroy(decoder);
            decoder = nullptr;
        }
        latest = nullptr;
        if (texture) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
        if (mutex) {
            SDL_DestroyMutex(mutex);
            mutex = nullptr;
        }
        head = tail = used = 0;
        paused = false;
        decodeMs = uploads = draws = 0;
    }
    void start(SDL_Renderer *r, const std::string &file) {
        stop();
        decoder = plm_create_with_filename(file.c_str());
        if (!decoder || !plm_has_headers(decoder)) {
            stop();
            throw std::runtime_error("Invalid openning.v");
        }
        width = plm_get_width(decoder);
        height = plm_get_height(decoder);
        if (width < 1 || width > 800 || height < 1 || height > 600 || plm_get_samplerate(decoder) != 44100) {
            stop();
            throw std::runtime_error("Opening format mismatch; check the original movie file");
        }
        texture = SDL_CreateTexture(r, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);
        yuv = texture != nullptr;
        if (!yuv)
            texture = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height);
        mutex = SDL_CreateMutex();
        if (!texture || !mutex) {
            stop();
            throw std::runtime_error(SDL_GetError());
        }
        if (yuv) {
            // Black until the first picture is decoded (limited-range Y 16, neutral chroma).
            int cw = (width + 1) / 2, ch = (height + 1) / 2;
            std::vector<uint8_t> luma(size_t(width) * height, 16), chroma(size_t(cw) * ch, 128);
            SDL_UpdateYUVTexture(texture, nullptr, luma.data(), width, chroma.data(), cw, chroma.data(), cw);
            pixels.clear();
        } else {
            pixels.assign(size_t(width) * height * 3, 0);
            SDL_UpdateTexture(texture, nullptr, pixels.data(), width * 3);
        }
        Mix_HaltMusic();
        Mix_HaltChannel(-1);
        Mix_HookMusic(mix, this);
        plm_set_video_decode_callback(decoder, videoFrame, this);
        plm_set_audio_decode_callback(decoder, audioFrame, this);
        // Enough buffered audio to ride out a slow frame; the ring holds one second.
        plm_set_audio_lead_time(decoder, 0.3);
        decoder->skip_b_frames_behind = 2.0 / plm_get_framerate(decoder);
        last = SDL_GetTicks();
    }
    bool draw(SDL_Renderer *r) {
        if (!decoder)
            return false;
        uint32_t now = SDL_GetTicks();
        double dt = std::min(0.25, (now - last) / 1000.0);
        last = now;
        if (!paused) {
            plm_decode(decoder, dt);
            upload();
            decodeMs += SDL_GetTicks() - now;
        }
        draws++;
        SDL_Rect dst{0, 0, 800, 600};
        SDL_RenderCopy(r, texture, nullptr, &dst);
        return !plm_has_ended(decoder);
    }
};

#endif
