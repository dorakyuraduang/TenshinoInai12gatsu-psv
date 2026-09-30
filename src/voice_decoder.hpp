#pragma once
#include "archive.hpp"
#include <SDL.h>
#include <SDL_mixer.h>
#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>
#include <cstring>
#include <limits>
#include <memory>

namespace tenshi {
namespace voice_detail {
constexpr size_t encodedLimit = 8u * 1024 * 1024;
constexpr size_t pcmLimit = 16u * 1024 * 1024;
constexpr size_t conversionBudget = 32u * 1024 * 1024;
struct MemoryOgg {
    const Bytes &bytes;
    size_t position = 0;
    static size_t read(void *destination, size_t unit, size_t count, void *source) {
        auto &s = *static_cast<MemoryOgg *>(source);
        if (!unit || !count)
            return 0;
        size_t items = std::min(count, (s.bytes.size() - s.position) / unit);
        size_t length = items * unit;
        if (length)
            std::memcpy(destination, s.bytes.data() + s.position, length);
        s.position += length;
        return items;
    }
    static int seek(void *source, ogg_int64_t offset, int whence) {
        auto &s = *static_cast<MemoryOgg *>(source);
        ogg_int64_t base = whence == SEEK_SET   ? 0
                           : whence == SEEK_CUR ? ogg_int64_t(s.position)
                           : whence == SEEK_END ? ogg_int64_t(s.bytes.size())
                                                : -1;
        if (base < 0 || offset < -base || offset > ogg_int64_t(s.bytes.size()) - base)
            return -1;
        s.position = size_t(base + offset);
        return 0;
    }
    static long tell(void *source) {
        return long(static_cast<MemoryOgg *>(source)->position);
    }
    static int close(void *) {
        return 0; // The caller owns the immutable encoded bytes.
    }
};
struct VorbisFile {
    OggVorbis_File file{};
    bool opened = false;
    ~VorbisFile() {
        if (opened)
            ov_clear(&file);
    }
};
} // namespace voice_detail

// SDL_mixer's Ogg Mix_LoadWAV_RW path holds the audio device lock for the whole
// decode, even on a background thread. Decode independently of the live mixer.
// The main thread supplies its format; this worker never queries or locks it.
inline Mix_Chunk *decodeVoicePcm(const Bytes &repairedOgg, int frequency, SDL_AudioFormat format,
                               int channels) {
    using namespace voice_detail;
    if (repairedOgg.empty() || repairedOgg.size() > encodedLimit ||
        repairedOgg.size() > size_t(std::numeric_limits<long>::max()))
        throw std::runtime_error("Invalid encoded voice size");
    if (frequency < 1 || frequency > 192000 || channels < 1 || channels > 8)
        throw std::runtime_error("Invalid voice mixer format");
    MemoryOgg source{repairedOgg};
    VorbisFile decoder;
    ov_callbacks callbacks{MemoryOgg::read, MemoryOgg::seek, MemoryOgg::close, MemoryOgg::tell};
    int result = ov_open_callbacks(&source, &decoder.file, nullptr, 0, callbacks);
    if (result < 0)
        throw std::runtime_error("Cannot open voice Vorbis stream: " + std::to_string(result));
    decoder.opened = true;
    const auto *info = ov_info(&decoder.file, 0);
    if (!info || info->rate < 1 || info->rate > 192000 || info->channels < 1 || info->channels > 8)
        throw std::runtime_error("Invalid voice Vorbis format");
    const int sourceRate = int(info->rate), sourceChannels = info->channels;
    const size_t frameBytes = size_t(sourceChannels) * sizeof(int16_t);
    Bytes pcm;
    ogg_int64_t total = ov_pcm_total(&decoder.file, -1);
    if (total > 0) {
        if (uint64_t(total) > pcmLimit / frameBytes)
            throw std::runtime_error("Voice PCM exceeds memory limit");
        pcm.reserve(size_t(total) * frameBytes);
    }
    char fragment[16384];
    for (;;) {
        int link = 0;
        long count = ov_read(&decoder.file, fragment, sizeof(fragment), SDL_BYTEORDER == SDL_BIG_ENDIAN,
                             sizeof(int16_t), 1, &link);
        if (count == 0)
            break;
        if (count < 0)
            throw std::runtime_error("Voice Vorbis decode failed: " + std::to_string(count));
        const auto *current = ov_info(&decoder.file, link);
        if (!current || current->rate != sourceRate || current->channels != sourceChannels)
            throw std::runtime_error("Voice Vorbis format changes inside one clip");
        if (size_t(count) % frameBytes || size_t(count) > pcmLimit - pcm.size())
            throw std::runtime_error("Invalid or oversized decoded voice PCM");
        pcm.insert(pcm.end(), fragment, fragment + count);
    }
    if (pcm.empty())
        throw std::runtime_error("Voice contains no PCM samples");
    ov_clear(&decoder.file);
    decoder.opened = false;
    // Every original clip is 44.1 kHz mono. Copy its S16 samples into both channels
    // directly; generic CVT uses a float stereo intermediate four times as large.
    const bool duplicate = sourceRate == frequency && format == AUDIO_S16SYS &&
                           sourceChannels == 1 && channels == 2;
    SDL_AudioCVT converter{};
    result = duplicate ? 0 : SDL_BuildAudioCVT(&converter, AUDIO_S16SYS, Uint8(sourceChannels), sourceRate,
                                              format, Uint8(channels), frequency);
    if (result < 0)
        throw std::runtime_error(std::string("Cannot convert voice format: ") + SDL_GetError());
    int multiplier = duplicate ? 2 : result ? converter.len_mult : 1;
    if (multiplier < 1 || pcm.size() > conversionBudget / size_t(multiplier))
        throw std::runtime_error("Converted voice PCM exceeds memory limit");
    size_t allocation = pcm.size() * size_t(multiplier);
    if (pcm.capacity() > conversionBudget || allocation > conversionBudget - pcm.capacity())
        throw std::runtime_error("Voice conversion exceeds combined PCM memory budget");
    std::unique_ptr<Uint8, decltype(&SDL_free)> storage(static_cast<Uint8 *>(SDL_malloc(allocation)),
                                                     SDL_free);
    if (!storage)
        throw std::runtime_error("Cannot allocate voice PCM");
    const size_t sourceLength = pcm.size();
    size_t length = duplicate ? allocation : sourceLength;
    if (duplicate) {
        for (size_t i = 0; i < sourceLength / sizeof(int16_t); i++) {
            std::memcpy(storage.get() + i * 4, pcm.data() + i * 2, sizeof(int16_t));
            std::memcpy(storage.get() + i * 4 + 2, pcm.data() + i * 2, sizeof(int16_t));
        }
    } else
        std::memcpy(storage.get(), pcm.data(), sourceLength);
    Bytes().swap(pcm); // Release the source buffer before running conversion filters.
    if (result) {
        converter.buf = storage.get();
        converter.len = int(sourceLength);
        if (SDL_ConvertAudio(&converter) < 0)
            throw std::runtime_error(std::string("Voice resampling failed: ") + SDL_GetError());
        if (converter.len_cvt <= 0 || size_t(converter.len_cvt) > allocation)
            throw std::runtime_error("Invalid resampled voice PCM size");
        length = size_t(converter.len_cvt);
    }
    std::unique_ptr<Mix_Chunk, decltype(&SDL_free)> chunk(
        static_cast<Mix_Chunk *>(SDL_malloc(sizeof(Mix_Chunk))), SDL_free);
    if (!chunk)
        throw std::runtime_error("Cannot allocate voice chunk");
    chunk->allocated = 1;
    chunk->abuf = storage.release();
    chunk->alen = Uint32(length);
    chunk->volume = MIX_MAX_VOLUME;
    return chunk.release();
}
} // namespace tenshi
