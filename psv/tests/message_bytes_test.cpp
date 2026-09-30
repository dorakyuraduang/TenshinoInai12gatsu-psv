#include "runtime.hpp"
#include <iostream>
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: message_bytes_test <message-bytes.jsonl>");
        std::ifstream input(argv[1]);
        if (!input)
            throw std::runtime_error("Missing .NET reference");
        std::string line;
        size_t count = 0;
        while (std::getline(input, line)) {
            auto record = tenshi::Json::parse(line);
            if (tenshi::gb18030ByteCount(record.at("text")) != record.at("bytes"))
                throw std::runtime_error("GB18030 byte count differs at record " + std::to_string(count));
            ++count;
        }
        if (!count)
            throw std::runtime_error("Empty reference");
        std::cout << count << " raw message byte counts match Godot .NET GB18030\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
