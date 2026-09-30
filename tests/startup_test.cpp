#include "runtime.hpp"
#include <iostream>
using namespace tenshi;
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Usage: startup_test <reference-directory> <original-resources>");
        auto reference = readJson(std::string(argv[1]) + "/startup.json");
        const int boundary = reference.at("introEvents");
        const auto &events = reference.at("events");
        if (boundary != reference.at("storyStart") || reference.at("firstLaunchStage") != "story" ||
            reference.at("returnLaunchStage") != "opening")
            throw std::runtime_error("Godot startup contract changed");
        Runtime first(argv[2]);
        Json e;
        for (int i = 0; i < int(events.size()); i++) {
            if (first.introComplete() != (i >= boundary))
                throw std::runtime_error("First-launch boundary differs at event " + std::to_string(i));
            if (!first.next(e) || e != events.at(i))
                throw std::runtime_error("Linear event differs from Godot");
            if (i == boundary - 2 || i == boundary - 1 || i == boundary + 1) {
                auto saved = first.save();
                Runtime restored(argv[2]);
                restored.load(saved);
                if (restored.save() != saved || restored.introComplete() != first.introComplete())
                    throw std::runtime_error("Legacy save index/boundary did not survive reload");
                first = std::move(restored);
            }
        }
        Runtime nextGame(argv[2]);
        nextGame.skipPrologue();
        if (!nextGame.introComplete() || nextGame.save().at("prologue") != boundary)
            throw std::runtime_error("New game skipped normal-story scripts");
        for (int i = boundary; i < int(events.size()); i++)
            if (!nextGame.next(e) || e != events.at(i))
                throw std::runtime_error("Normal story after intro differs from Godot");
        std::cout << "Godot startup parity passed: " << boundary << " intro events, " << events.size()
                  << " base events; normal-story start, restart contract and legacy save positions\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
