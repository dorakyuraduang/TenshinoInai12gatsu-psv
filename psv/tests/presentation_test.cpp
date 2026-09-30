#include "presentation.hpp"
#include "runtime.hpp"
#include <iostream>
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: presentation_test <reference-directory>");
        std::ifstream f(std::string(argv[1]) + "/presentation.jsonl");
        if (!f)
            throw std::runtime_error("Missing oracle");
        std::string line;
        size_t count = 0, glyphs = 0;
        while (std::getline(f, line)) {
            auto j = tenshi::Json::parse(line);
            tenshi::NativeMessageLayout layout;
            layout.set(j.at("text"), j.at("full"));
            tenshi::Json pages = tenshi::Json::array();
            for (const auto &p : layout.pages) {
                auto row = tenshi::Json::array();
                for (const auto &g : p) {
                    row.push_back({g.text, g.x, g.y, g.advance, g.offset, g.size});
                    glyphs++;
                }
                pages.push_back(row);
            }
            if (pages != j.at("pages"))
                throw std::runtime_error("Godot message layout mismatch, case " + std::to_string(count));
            count++;
        }
        auto effects = tenshi::readJson(std::string(argv[1]) + "/effects.json");
        for (const auto &e : effects) {
            auto actual = tenshi::effectSample(e.at("effect"), e.at("frame"));
            if (actual.x != e.at("x") || actual.y != e.at("y") ||
                std::abs(actual.zoom - e.at("zoom").get<float>()) > 1e-6 ||
                std::abs(actual.flash - e.at("flash").get<float>()) > 1e-6)
                throw std::runtime_error("Godot effect sample mismatch");
        }
        std::cout << count << " layouts, " << glyphs << " glyph positions and " << effects.size()
                  << " effect samples match Godot\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
