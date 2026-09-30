#include "json.hpp"
#include "media.hpp"
#include <iostream>
using namespace tenshi;
uint64_t hash(const Bytes &data) {
    uint64_t h = 14695981039346656037ull;
    for (auto b : data)
        h = (h ^ b) * 1099511628211ull;
    return h;
}
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Usage: media_test <reference-json> <original-resources>");
        std::ifstream f(argv[1]);
        nlohmann::json rows;
        f >> rows;
        std::map<std::string, Archive> archives;
        size_t count = 0;
        for (const auto &row : rows) {
            std::string file = row.at("file"), name = row.at("name");
            if (!archives.count(file))
                archives.emplace(file, Archive(std::string(argv[2]) + "/" + file));
            auto &a = archives.at(file);
            uint64_t actual;
            if (row.contains("frame")) {
                int frame = row.at("frame");
                auto image = (file == "egbg.a" || frame == -2) ? background(a, name)
                             : frame < 0                       ? pxStandalone(a.read(name))
                                                               : pxFrame(lzss(a.read(name)), frame);
                if (image.w != row.at("w") || image.h != row.at("h") ||
                    (file != "egbg.a" && frame != -2 && (image.x != row.at("x") || image.y != row.at("y"))))
                    throw std::runtime_error("Image geometry mismatch: " + file + "/" + name);
                actual = hash(image.rgba);
            } else if (file == "voice.a")
                actual = hash(repairedVoice(a.read(name)));
            else {
                auto b = wave(a.read(name));
                actual = hash(Bytes(b.begin() + 44, b.end()));
            }
            if (actual != row.at("hash").get<uint64_t>())
                throw std::runtime_error("Decoded bytes mismatch: " + file + "/" + name);
            count++;
        }
        std::cout << count << " media records match C# decoded bytes\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
