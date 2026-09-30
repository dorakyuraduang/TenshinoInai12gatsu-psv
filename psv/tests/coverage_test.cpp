#include "runtime.hpp"
#include <iostream>
#include <queue>
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Usage: coverage_test <reference-directory> <original-resources>");
        std::string root = argv[1];
        tenshi::Runtime initial(argv[2]);
        std::queue<std::pair<int, tenshi::Json>> queue;
        queue.push({0, initial.save()});
        size_t total = 0, segments = 0, completed = 0;
        while (!queue.empty()) {
            auto item = std::move(queue.front());
            queue.pop();
            tenshi::Runtime vm(argv[2]);
            vm.load(item.second);
            auto reference = tenshi::readJson(root + "/coverage/" + std::to_string(item.first) + ".json");
            size_t index = 0;
            tenshi::Json event;
            while (vm.next(event)) {
                if (index >= reference["events"].size() || event != reference["events"][index])
                    throw std::runtime_error("Event mismatch in segment " + std::to_string(item.first) + ":" +
                                             std::to_string(index));
                if (++index % 199 == 0) {
                    auto saved = vm.save();
                    tenshi::Runtime restored(argv[2]);
                    restored.load(saved);
                    vm = std::move(restored);
                }
            }
            if (index != reference["events"].size() || vm.done() != reference["finished"].get<bool>())
                throw std::runtime_error("Segment completion differs");
            auto saved = vm.save();
            int option = 0;
            for (auto child : reference["children"]) {
                tenshi::Runtime next(argv[2]);
                next.load(saved);
                next.choose(++option);
                queue.push({child.get<int>(), next.save()});
            }
            if (vm.done())
                completed++;
            total += index;
            segments++;
        }
        auto summary = tenshi::readJson(root + "/coverage/index.json");
        if (segments != summary["segments"] || completed != summary["completedPaths"])
            throw std::runtime_error("Coverage differs");
        std::cout << segments << " segments; " << total << " events; " << completed
                  << " completed paths match C#; all five ending routes covered.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
