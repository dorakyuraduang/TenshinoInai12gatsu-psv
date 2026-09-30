#include "runtime.hpp"
#include <iostream>
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Usage: runtime_test <reference-directory> <original-resources>");
        for (int p = 1; p <= 3; p++) {
            std::string root = argv[1];
            auto trace = tenshi::readJson(root + "/trace" + std::to_string(p) + ".json");
            tenshi::Runtime vm(argv[2]);
            size_t index = 0, decision = 0, recovered = 0;
            tenshi::Json e;
            while (vm.next(e)) {
                if (index >= trace["events"].size() || e != trace["events"][index]) {
                    std::cerr << "Mismatch at " << index << "\nactual: " << e.dump()
                              << "\nexpected: " << trace["events"].at(index).dump() << "\n";
                    return 1;
                }
                if (e["kind"] == tenshi::Dialogue) {
                    auto text = e.at("text").get<std::string>();
                    auto got = vm.savedDialogue(text, e.at("voice"));
                    if (got != e)
                        throw std::runtime_error("Legacy dialogue metadata differs at event " +
                                                 std::to_string(index) + ": " +
                                                 e.at("source").get<std::string>() + ":" +
                                                 std::to_string(e.at("value").get<int>()));
                    recovered++;
                }
                if (e["kind"] == tenshi::Choice)
                    vm.choose(trace["decisions"].at(decision++));
                if (index % 137 == 0) {
                    auto s = vm.save();
                    tenshi::Runtime restored(argv[2]);
                    restored.load(s);
                    if (restored.save() != s)
                        throw std::runtime_error("Save roundtrip differs");
                    vm = std::move(restored);
                }
                index++;
            }
            if (index != trace["events"].size() || !vm.done())
                throw std::runtime_error("Incomplete route");
            std::cout << "Policy " << p << ": " << index << " events match C# reference; save/reload passed; "
                      << recovered << " legacy dialogue states recovered\n";
        }
        return 0;
    } catch (std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
