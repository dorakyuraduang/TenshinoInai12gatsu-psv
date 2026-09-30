#define SDL_MAIN_HANDLED
#include "archive_audio.hpp"
#include <iostream>
using namespace tenshi;
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: archive_audio_test <original-resources>");
        size_t checked = 0;
        for (const char *file : {"music.a", "se.a"}) {
            Archive a(std::string(argv[1]) + "/" + file);
            for (const auto &entry : a.entries) {
                if (entry.first.size() < 2 || entry.first.substr(entry.first.size() - 2) != ".w")
                    continue;
                auto expected = wave(a.read(entry.first));
                auto *rw = ArchiveWave::open(a, entry.first);
                if (SDL_RWsize(rw) != Sint64(expected.size()))
                    throw std::runtime_error("Stream size differs");
                for (size_t at : {size_t(0), size_t(17), size_t(40), size_t(43), size_t(44),
                                  expected.size() / 2, expected.size() - 3}) {
                    for (size_t unit : {size_t(1), size_t(2), size_t(4)}) {
                        if (SDL_RWseek(rw, Sint64(at), RW_SEEK_SET) != Sint64(at))
                            throw std::runtime_error("Seek differs");
                        Bytes got(257 * unit);
                        size_t count = SDL_RWread(rw, got.data(), unit, 257),
                               wanted = std::min(size_t(257), (expected.size() - at) / unit);
                        if (count != wanted ||
                            !std::equal(got.begin(), got.begin() + count * unit, expected.begin() + at))
                            throw std::runtime_error("Stream bytes differ");
                    }
                }
                if (SDL_RWseek(rw, 1, RW_SEEK_END) != -1 || SDL_RWseek(rw, -1, RW_SEEK_SET) != -1)
                    throw std::runtime_error("Entry boundary was not enforced");
                SDL_RWclose(rw);
                if (std::string(file) == "music.a") {
                    const uint32_t rate = u32(expected, 24), block = u16(expected, 32);
                    const size_t frames = (expected.size() - 44) / block;
                    for (size_t frame : {size_t(0), frames / 2, frames - 10, frames + 10})
                        for (bool loop : {false, true}) {
                            double seconds = 0.3, position = (frame + 0.01) / double(rate);
                            auto got = musicOverlap(a, entry.first, position, seconds, loop);
                            if (!loop && frame >= frames) {
                                if (!got.empty())
                                    throw std::runtime_error("Finished one-shot overlap wrapped");
                                continue;
                            }
                            size_t wanted = size_t(seconds * rate);
                            if (!loop)
                                wanted = std::min(wanted, frames - frame);
                            if (got.size() != 44 + wanted * block || u32(got, 40) != wanted * block)
                                throw std::runtime_error("Overlap length differs");
                            for (size_t i = 0; i < wanted * block; ++i)
                                if (got[44 + i] != expected[44 + (frame * block + i) % (frames * block)])
                                    throw std::runtime_error("Overlap samples differ");
                        }
                }
                checked++;
            }
        }
        std::cout << checked
                  << " archive audio streams: header, seek, partial read and bounds passed; all music "
                     "overlaps/loop boundaries matched PCM\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
