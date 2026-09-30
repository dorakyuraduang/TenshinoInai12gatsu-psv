#include "runtime.hpp"
#include <iostream>
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Usage: replay_test <reference-directory> <resources>");
        auto traces = tenshi::readJson(std::string(argv[1]) + "/replays.json");
        size_t total = 0;
        for (auto &t : traces) {
            tenshi::Runtime vm(argv[2]);
            vm.startReplay(t.at("index"));
            tenshi::Json e;
            size_t at = 0;
            while (vm.next(e)) {
                if (at >= t.at("events").size() || e != t.at("events").at(at))
                    throw std::runtime_error("Replay mismatch");
                if (e.at("kind") == tenshi::Choice)
                    vm.choose(1);
                if (at % 137 == 0) {
                    auto save = vm.save();
                    tenshi::Runtime restored(argv[2]);
                    restored.load(save);
                    vm = std::move(restored);
                }
                at++;
            }
            if (!vm.done() || at != t.at("events").size())
                throw std::runtime_error("Incomplete replay");
            total += at;
        }
        std::cout << traces.size() << " replay entrypoints, " << total
                  << " events match Godot and survive save/reload\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
