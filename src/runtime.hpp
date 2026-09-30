#pragma once
#include "archive.hpp"
#include "json.hpp"
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace tenshi {
using Json = nlohmann::json;
using Words = std::vector<uint16_t>;
inline Json readJson(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot open " + path);
    f.seekg(0, std::ios::end);
    auto size = f.tellg();
    if (size < 0 || size > 32 * 1024 * 1024)
        throw std::runtime_error("Invalid JSON size: " + path);
    f.seekg(0);
    Json j;
    f >> j;
    return j;
}
enum Kind {
    Background,
    Speaker,
    Dialogue,
    Music,
    ClearCharacters,
    Character,
    HideCharacter,
    TextMode,
    MessageWindow,
    WaitMessage,
    ClearMessage,
    MessageLayout,
    Pause,
    PauseFrames,
    ScreenEffect,
    Weather,
    Sound,
    StopSound,
    Transition,
    Choice,
    Ending,
    UnlockMemoir
};
inline Json event(int kind, int value = 0, const std::string &source = "") {
    return {{"kind", kind}, {"value", value}, {"text", ""},       {"voice", ""},
            {"slot", -1},   {"flags", 3},     {"source", source}, {"options", nullptr}};
}
#include "script.hpp"
class Runtime {
    struct Frame {
        int module, ip;
        bool branch;
    };
    std::string root;
    Archive archive;
    Json manifest, prologue;
    std::map<int, Json> scripts;
    std::map<int, int32_t> variables, voices;
    std::vector<Frame> stack;
    std::deque<Json> pending;
    int module = 40, ip = 0, prologueAt = 0, introEnd = 0, choiceCount = 0;
    bool finished = false, waiting = false, replay = false;
    Json &script() {
        if (!scripts.count(module)) {
            if (module < 21 || module >= int(manifest.at("modules").size()))
                fail("Invalid module");
            // Small cache: scenario files are loaded lazily instead of keeping the entire game in RAM.
            if (scripts.size() >= 8)
                scripts.clear();
            scripts[module] =
                OriginalScript(archive, manifest.at("modules").at(module).get<std::string>()).compiled();
        }
        return scripts.at(module);
    }
    [[noreturn]] void fail(const std::string &s) const {
        throw std::runtime_error("Module " + std::to_string(module) + ":" + std::to_string(ip) + ": " + s);
    }
    uint16_t word(const Json &code, int at) {
        if (at < 0 || at >= int(code.size()))
            fail("Truncated bytecode");
        return code.at(at).get<uint16_t>();
    }
    Words readWords() {
        auto &c = script().at("code");
        Words w;
        while (ip < int(c.size()) && word(c, ip) != 0x203a) {
            uint16_t t = word(c, ip++);
            w.push_back(t);
            int n = 0;
            switch (t) {
            case 0x6923:
            case 0x7323:
            case 0x6924:
            case 0x7324:
            case 0x6d24:
            case 0x737e:
            case 0x6d40:
            case 0x7340:
                n = 1;
                break;
            case 0x6c23:
            case 0x6143:
            case 0x6643:
            case 0x6c43:
                n = 2;
                break;
            }
            while (n--)
                w.push_back(word(c, ip++));
        }
        if (ip < int(c.size()))
            ip++;
        return w;
    }
    int skipAt(int at) {
        auto &c = script().at("code");
        if (word(c, at) == 0x7b)
            return word(c, at + 1) + 2;
        int saved = ip;
        ip = at;
        readWords();
        int end = ip;
        ip = saved;
        return end;
    }
    static int priority(uint16_t op) {
        switch (op) {
        case 0x7c7c:
            return 1;
        case 0x2626:
            return 2;
        case 0x7c:
            return 3;
        case 0x5e:
            return 4;
        case 0x26:
            return 5;
        case 0x3d3d:
        case 0x3d21:
            return 6;
        case 0x3e:
        case 0x3c:
        case 0x3d3e:
        case 0x3d3c:
            return 7;
        case 0x3c3c:
        case 0x3e3e:
            return 8;
        case 0x2b:
        case 0x2d:
            return 9;
        case 0x2a:
        case 0x2f:
        case 0x25:
            return 10;
        default:
            return -1;
        }
    }
    int32_t evaluate(const Words &w) {
        size_t p = 0;
        int depth = 0;
        auto take = [&]() {
            if (p >= w.size())
                fail("Truncated expression");
            return w[p++];
        };
        std::function<int32_t(int)> expression;
        std::function<int32_t()> atom;
        atom = [&]() -> int32_t {
            if (++depth > 128)
                fail("Expression nesting limit");
            int32_t result = 0;
            if (p < w.size())
                switch (take()) {
                case 0x6923:
                    result = take();
                    break;
                case 0x6924:
                    result = variables[take()];
                    break;
                case 0x6c23: {
                    uint32_t lo = take(), hi = take();
                    result = int32_t(lo | (hi << 16));
                    break;
                }
                case 0x21:
                    result = !atom();
                    break;
                case 0x7e:
                    result = ~atom();
                    break;
                case 0x2d:
                    result = int32_t(0u - uint32_t(atom()));
                    break;
                case 0x28:
                    result = expression(0);
                    if (take() != 0x29)
                        fail("Unclosed expression");
                    break;
                default:
                    fail("Unknown expression token");
                }
            depth--;
            return result;
        };
        expression = [&](int minimum) -> int32_t {
            int32_t l = atom();
            while (p < w.size() && priority(w[p]) >= minimum) {
                uint16_t op = take();
                int32_t r = expression(priority(op) + 1);
                uint32_t a = uint32_t(l), b = uint32_t(r);
                switch (op) {
                case 0x7c7c:
                    l = l || r;
                    break;
                case 0x2626:
                    l = l && r;
                    break;
                case 0x7c:
                    l |= r;
                    break;
                case 0x26:
                    l &= r;
                    break;
                case 0x5e:
                    l ^= r;
                    break;
                case 0x3d3d:
                    l = l == r;
                    break;
                case 0x3d21:
                    l = l != r;
                    break;
                case 0x3e:
                    l = l > r;
                    break;
                case 0x3c:
                    l = l < r;
                    break;
                case 0x3d3e:
                    l = l >= r;
                    break;
                case 0x3d3c:
                    l = l <= r;
                    break;
                case 0x3c3c:
                    l = int32_t(a << (b & 31));
                    break;
                case 0x3e3e:
                    l = l >> (b & 31);
                    break;
                case 0x2b:
                    l = int32_t(a + b);
                    break;
                case 0x2d:
                    l = int32_t(a - b);
                    break;
                case 0x2a:
                    l = int32_t(a * b);
                    break;
                case 0x2f:
                case 0x25:
                    if (r == 0 || (l == INT32_MIN && r == -1))
                        fail("Invalid division");
                    l = op == 0x2f ? l / r : l % r;
                    break;
                default:
                    fail("Unknown operator");
                }
            }
            return l;
        };
        int32_t result = expression(0);
        if (p != w.size())
            fail("Trailing expression operands");
        return result;
    }
    void pop() {
        if (stack.empty())
            fail("Empty stack");
        auto f = stack.back();
        stack.pop_back();
        module = f.module;
        ip = f.ip;
    }
    void ret() {
        while (!stack.empty() && stack.back().branch)
            stack.pop_back();
        if (stack.empty())
            finished = true;
        else
            pop();
    }
    std::string name() const {
        return manifest.at("modules").at(module).get<std::string>();
    }
    void emit(int start) {
        auto &all = script().at("events");
        auto key = std::to_string(start);
        if (!all.contains(key))
            return;
        for (auto e : all.at(key)) {
            if (e.at("kind") == Dialogue && !e.at("voice").get<std::string>().empty()) {
                auto n = name();
                n = n.substr(0, n.find_last_of('.'));
                char num[32];
                snprintf(num, sizeof(num), "_%03d.g", ++voices[module]);
                e["voice"] = n + num;
            }
            pending.push_back(e);
        }
    }

  public:
    explicit Runtime(std::string data) : root(std::move(data)), archive(root + "/tenshi_dvd.a") {
        auto info = lzss(archive.read("projectInfo"));
        if (info.size() < 68 || (info.size() - 68) % 28)
            throw std::runtime_error("Invalid module table");
        Json names = Json::array({""});
        for (size_t p = 68; p < info.size(); p += 28) {
            size_t n = 0;
            while (n < 28 && info[p + n])
                n++;
            names.push_back(std::string(reinterpret_cast<char *>(info.data() + p), n) + ".p");
        }
        // Fingerprint the script archive to reject saves from different editions.
        uint64_t hash = 14695981039346656037ull;
        std::ifstream f(archive.path, std::ios::binary);
        std::array<char, 16384> buffer{};
        while (f) {
            f.read(buffer.data(), buffer.size());
            for (std::streamsize i = 0; i < f.gcount(); i++) {
                hash ^= uint8_t(buffer[i]);
                hash *= 1099511628211ull;
            }
        }
        manifest = {{"format", 2},       {"complete", true},
                    {"modules", names},  {"characters", OriginalScript(archive, "%CharDef.p").strings},
                    {"startModule", 40}, {"scriptHash", "original-fnv1a64-" + std::to_string(hash)}};
        prologue = Json::array();
        for (const char *name : {"com_0100.p", "com_0110.p", "com_0120.p"}) {
            for (auto &e : OriginalScript(archive, name).linear())
                prologue.push_back(e);
            // Godot's _prologueEventCount ends after com_0100 only. The other
            // two linear scripts are the beginning of the normal story.
            if (std::string(name) == "com_0100.p")
                introEnd = int(prologue.size());
        }
    }
    void startReplay(int index) {
        auto code = OriginalScript(archive, "_omakeMenu.p").code;
        size_t at = 4124;
        while (at < code.size() && code.at(at) != 0x7753)
            at++;
        if (at + 1 >= code.size())
            fail("Replay switch missing");
        size_t table = code.at(at + 1);
        int count = code.at(table + 1);
        for (int i = 0; i < count; i++) {
            int target = code.at(table + 2 + i * 2);
            if (!target || code.at(table + 3 + i * 2) != index)
                continue;
            if (code.at(target) != 0x6143)
                fail("Invalid replay entry");
            module = code.at(target + 1);
            ip = 0;
            prologueAt = int(prologue.size());
            choiceCount = 0;
            finished = waiting = false;
            replay = true;
            variables.clear();
            voices.clear();
            stack.clear();
            pending.clear();
            scripts.clear();
            return;
        }
        fail("Unknown replay index");
    }
    void skipPrologue() {
        prologueAt = introEnd;
    }
    bool introComplete() const {
        return prologueAt >= introEnd;
    }
    bool done() const {
        return finished && pending.empty();
    }
    bool choosing() const {
        return waiting;
    }
    const Json &metadata() const {
        return manifest;
    }
    void choose(int option) {
        if (!waiting || option < 1 || option > choiceCount)
            fail("Invalid choice");
        variables[218] = option;
        waiting = false;
    }
    Json save() const {
        Json frames = Json::array();
        for (auto f : stack)
            frames.push_back({f.module, f.ip, f.branch});
        Json vars = Json::object(), vs = Json::object();
        for (auto p : variables)
            vars[std::to_string(p.first)] = p.second;
        for (auto p : voices)
            vs[std::to_string(p.first)] = p.second;
        return {
            {"replay", replay},       {"module", module},       {"ip", ip},
            {"prologue", prologueAt}, {"choices", choiceCount}, {"finished", finished},
            {"waiting", waiting},     {"vars", vars},           {"voices", vs},
            {"stack", frames},        {"pending", pending},     {"scriptHash", manifest.at("scriptHash")}};
    }
    void load(const Json &s) {
        if (s.at("scriptHash") != manifest.at("scriptHash"))
            fail("Save belongs to different game resources");
        int m = s.at("module"), pos = s.at("ip"), pro = s.at("prologue");
        if (m < 21 || m >= int(manifest.at("modules").size()) || pos < 0 || pro < 0 ||
            pro > int(prologue.size()) || s.at("stack").size() > 1024)
            fail("Invalid save");
        replay = s.value("replay", false);
        module = m;
        ip = pos;
        prologueAt = pro;
        choiceCount = s.at("choices");
        finished = s.at("finished");
        waiting = s.at("waiting");
        variables.clear();
        voices.clear();
        stack.clear();
        pending.clear();
        scripts.clear();
        for (auto it = s.at("vars").begin(); it != s.at("vars").end(); ++it)
            variables[std::stoi(it.key())] = it.value();
        for (auto it = s.at("voices").begin(); it != s.at("voices").end(); ++it)
            voices[std::stoi(it.key())] = it.value();
        for (auto f : s.at("stack"))
            stack.push_back({f[0], f[1], f[2]});
        for (auto e : s.at("pending"))
            pending.push_back(e);
    }
    // Recover dialogue metadata from legacy saves that predate frontend message state.
    Json savedDialogue(const std::string &displayText, const std::string &voiceName) {
        auto matches = [&](const Json &e) {
            if (e.at("kind") != Dialogue)
                return false;
            std::string text = e.at("text");
            while (!text.empty() && text.front() == '\x7f')
                text.erase(0, 1);
            return !text.empty() && displayText.size() >= text.size() &&
                   displayText.compare(displayText.size() - text.size(), text.size(), text) == 0;
        };
        if (prologueAt < int(prologue.size()) || (module == 40 && ip == 0)) {
            for (int i = std::min(prologueAt, int(prologue.size())) - 1; i >= 0; --i)
                if (matches(prologue[i]))
                    return prologue[i];
        }
        Json result = nullptr;
        int closest = -1;
        for (auto it = script().at("events").begin(); it != script().at("events").end(); ++it) {
            int at = std::stoi(it.key());
            if (at >= ip || at < closest)
                continue;
            for (const auto &e : it.value())
                if (matches(e)) {
                    result = e;
                    closest = at;
                }
        }
        if (!result.is_null())
            result["voice"] = voiceName;
        return result;
    }
    bool next(Json &out) {
        if (prologueAt < int(prologue.size())) {
            out = prologue.at(prologueAt++);
            return true;
        }
        if (!pending.empty()) {
            out = pending.front();
            pending.pop_front();
            return true;
        }
        if (finished || waiting)
            return false;
        for (int guard = 0; guard < 100000; guard++) {
            if (stack.size() > 1024)
                fail("Call stack limit");
            auto &c = script().at("code");
            if (ip >= int(c.size())) {
                ret();
                if (finished)
                    return false;
                continue;
            }
            int start = ip;
            uint16_t op = word(c, ip++);
            switch (op) {
            case 0x6e46:
            case 0x7b:
            case 0x7d:
                ip++;
                break;
            case 0x203a:
                break;
            case 0x7452:
                ret();
                if (finished)
                    return false;
                break;
            case 0x7254: {
                int m = word(c, ip++), entry = word(c, ip++);
                while (!stack.empty() && stack.back().branch)
                    stack.pop_back();
                module = m;
                ip = entry;
                break;
            }
            case 0x7342:
                ip++;
                if (stack.empty() || !stack.back().branch)
                    fail("Switch return without frame");
                pop();
                break;
            case 0x7753: {
                int table = word(c, ip++);
                int value = evaluate(readWords());
                if (word(c, table) != 0x7345)
                    fail("Invalid switch table");
                int count = word(c, table + 1), target = -1, fallback = -1;
                for (int i = 0; i < count; i++) {
                    int pos = word(c, table + 2 + i * 2), match = word(c, table + 3 + i * 2);
                    if (pos == 0)
                        fallback = match;
                    else if (match == value)
                        target = pos;
                }
                ip = table + 2 + count * 2;
                if (target < 0)
                    target = fallback;
                if (target >= 0) {
                    stack.push_back({module, ip, true});
                    ip = target;
                }
                break;
            }
            case 0x6649: {
                bool condition = evaluate(readWords()) != 0;
                int end = skipAt(ip);
                if (!condition) {
                    ip = end;
                    if (ip < int(c.size()) && word(c, ip) == 0x6c45)
                        ip++;
                }
                break;
            }
            case 0x6c45:
                ip = skipAt(ip);
                break;
            case 0x6143: {
                int m = word(c, ip++), entry = word(c, ip++);
                Words args = readWords();
                if (replay && m == 8 && entry == 1426) {
                    finished = true;
                    return false;
                }
                if (m >= 21 && manifest.at("modules").at(m).get<std::string>().at(0) != '~') {
                    stack.push_back({module, ip, false});
                    module = m;
                    ip = entry;
                } else if (m == 13 && entry == 13767) {
                    Json options = Json::array();
                    std::string text;
                    for (size_t i = 0; i < args.size(); i++) {
                        auto token = args.at(i);
                        if (token == 0x2c) {
                            options.push_back(text);
                            text.clear();
                        } else if (token == 0x23)
                            continue;
                        else if (token == 0x7323)
                            text += script().at("strings").at(args.at(++i)).get<std::string>();
                        else if (token == 0x7324)
                            text += args.at(++i) == 59 ? "\xee\x80\x80" : "\xee\x80\x81";
                        else
                            fail("Unsupported choice text");
                    }
                    options.push_back(text);
                    choiceCount = int(options.size());
                    waiting = true;
                    out = event(Choice, start, name());
                    out["options"] = options;
                    return true;
                } else if (m == 20 && entry == 1188) {
                    out = event(Ending, evaluate(args), name());
                    return true;
                } else if (m == 7 && entry == 2090 && args.size() == 8)
                    voices[module] = args.at(7) - 1;
                else
                    emit(start);
                break;
            }
            case 0x6924: {
                int v = word(c, ip++);
                auto operation = word(c, ip++);
                int32_t val = evaluate(readWords());
                uint32_t prev = uint32_t(variables[v]);
                switch (operation) {
                case 0x3d:
                    variables[v] = val;
                    break;
                case 0x3d2b:
                    variables[v] = int32_t(prev + uint32_t(val));
                    break;
                case 0x3d2d:
                    variables[v] = int32_t(prev - uint32_t(val));
                    break;
                case 0x3d7c:
                    variables[v] = int32_t(prev | uint32_t(val));
                    break;
                case 0x3d26:
                    variables[v] = int32_t(prev & uint32_t(val));
                    break;
                case 0x2b2b:
                    variables[v] = int32_t(prev + 1);
                    break;
                case 0x2d2d:
                    variables[v] = int32_t(prev - 1);
                    break;
                default:
                    fail("Unknown assignment");
                }
                break;
            }
            case 0x7345:
                ip += word(c, ip) * 2 + 1;
                break;
            case 0x7953:
                if (evaluate(readWords()) == 0x051b6eef) {
                    finished = true;
                    return false;
                }
                fail("Unknown system command");
            default:
                if ((op & 255) == 1) {
                    readWords();
                    emit(start);
                } else
                    fail("Unknown instruction " + std::to_string(op));
                break;
            }
            if (!pending.empty()) {
                out = pending.front();
                pending.pop_front();
                return true;
            }
        }
        fail("Instruction limit");
    }
};
} // namespace tenshi
