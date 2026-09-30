#include "movie_index.hpp"
#include <iostream>
int main(int argc, char **argv) {
    try {
        if (argc != 4)
            throw std::runtime_error("usage: movie_index_test input.mp4 output.h264 output.aac");
        MovieIndex index;
        index.open(argv[1]);
        std::vector<uint8_t> packet;
        FILE *v = fopen(argv[2], "wb");
        FILE *a = fopen(argv[3], "wb");
        if (!v || !a)
            throw std::runtime_error("Cannot open test output");
        for (size_t i = 0; i < index.video.size(); ++i) {
            index.read(index.video[i], packet, true, i == 0);
            fwrite(packet.data(), 1, packet.size(), v);
        }
        for (auto &p : index.audio) {
            index.read(p, packet);
            uint32_t n = uint32_t(packet.size() + 7);
            uint8_t adts[7] = {
                0xff, 0xf1, 0x50, uint8_t(0x80 | (n >> 11)), uint8_t(n >> 3), uint8_t((n << 5) | 0x1f), 0xfc};
            fwrite(adts, 1, 7, a);
            fwrite(packet.data(), 1, packet.size(), a);
        }
        fclose(v);
        fclose(a);
        std::cout << "Index reader extracted " << index.video.size() << " AVC and " << index.audio.size()
                  << " AAC access units\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
