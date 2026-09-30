// Included inside namespace tenshi after the scene event definitions.
struct OriginalScript {
    std::string name;
    Words code;
    std::vector<std::string> strings;
    OriginalScript(const Archive &a, const std::string &n) : name(n) {
        auto b = a.read(n);
        size_t count = u16(b, 0), table = 2 + count * 2;
        requireBytes(b, 2, count * 2);
        for (size_t i = 0; i < count; i++)
            code.push_back(u16(b, 2 + i * 2));
        size_t texts = u16(b, table), base = table + texts * 2;
        requireBytes(b, table, texts * 2);
        for (size_t i = 0; i < texts; i++) {
            size_t start = base + (i ? u16(b, table + i * 2) : 0),
                   end = i + 1 < texts ? base + u16(b, table + (i + 1) * 2) : b.size();
            if (start > end)
                throw std::runtime_error("Invalid script string offsets");
            requireBytes(b, start, end - start);
            while (end > start && (b[end - 1] == 0 || b[end - 1] == 0x7f))
                end--;
            strings.push_back(gb18030(b.data() + start, end - start));
        }
    }
    static int operands(uint16_t t) {
        switch (t) {
        case 0x6923:
        case 0x7323:
        case 0x6924:
        case 0x7324:
        case 0x6d24:
        case 0x737e:
        case 0x6d40:
        case 0x7340:
            return 1;
        case 0x6c23:
        case 0x6143:
        case 0x6643:
        case 0x6c43:
            return 2;
        default:
            return 0;
        }
    }
    static std::vector<int> literals(const Words &w, size_t p) {
        std::vector<int> out;
        auto literal = [&]() {
            auto t = w.at(p++);
            if (t != 0x6923 && t != 0x6c23)
                throw std::runtime_error("Nonliteral image expression");
            uint32_t v = w.at(p++);
            if (t == 0x6c23)
                v |= uint32_t(w.at(p++)) << 16;
            return v;
        };
        while (p < w.size()) {
            uint32_t v = literal();
            while (p < w.size() && w[p] != 0x2c) {
                auto op = w.at(p++);
                if (op != 0x2b && op != 0x2d)
                    throw std::runtime_error("Invalid image expression");
                auto x = literal();
                v = op == 0x2b ? v + x : v - x;
            }
            out.push_back(int32_t(v));
            if (p < w.size())
                p++;
        }
        return out;
    }
    Json statement(const Words &w, int &voice) const {
        Json out = Json::array();
        if (w.empty())
            return out;
        int op = w[0], m = w.size() > 1 ? w[1] : 0, e = w.size() > 2 ? w[2] : 0;
        auto add = [&](int k, int v, int slot = -1, int flags = 3) {
            auto j = event(k, v);
            j["slot"] = slot;
            j["flags"] = flags;
            out.push_back(j);
        };
        bool call = op == 0x6143 && w.size() >= 3;
        if (call && m == 8 && (e == 1444 || e == 1607)) {
            int flags = 0;
            if (e == 1444)
                for (size_t i = 3; i < w.size(); i++) {
                    if (w[i] == 0x28 || w[i] == 0x29 || w[i] == 0x2b)
                        continue;
                    if (w[i] != 0x6923)
                        throw std::runtime_error("Invalid weather expression");
                    flags += w.at(++i);
                }
            add(Weather, flags);
        } else if (call && m == 8 && e == 1637)
            add(PauseFrames, w.size() >= 5 ? w[4] : 90);
        else if (call && m == 8 && (e == 4959 || e == 5251 || e == 6036 || e == 6200))
            add(ScreenEffect, e);
        else if (call && m == 8 && (e == 4905 || e == 4930 || e == 1393))
            add(MessageLayout, e == 4930 ? 1 : 0);
        else if (call && m == 7 && (e == 1945 || e == 1956 || e == 1967))
            add(TextMode, e == 1967 ? 0 : e == 1945 ? 1 : 3);
        else if (call && m == 7 && (e == 1978 || e == 2044))
            add(MessageWindow, e == 2044 ? 1 : 0);
        else if (op == 0xfe01 || op == 0xfd01)
            add(WaitMessage, op == 0xfe01 ? 1 : 0);
        else if (op == 0xfc01)
            add(ClearMessage, 0);
        else if (op == 0xfa01 && w.size() >= 3 && w[1] == 0x6923)
            add(Transition, w[2], w.size() >= 6 && w[4] == 0x6923 ? w[5] : 0);
        else if (call && m == 7 && e == 2213)
            add(Pause, w.size() >= 5 ? w[4] : 10);
        else if (call && m == 5 && e == 4 && w.size() >= 8 && w[3] == 0x6923 && w[6] == 0x6923)
            add(Sound, w[7], w[4]);
        else if (call && m == 5 && e == 156 && w.size() >= 5)
            add(StopSound, w[4], w.size() >= 8 ? w[7] : 0, w.size() >= 11 ? w[10] : 0);
        else if (call && m == 5 && e == 675 && w.size() >= 5)
            add(StopSound, w[4]);
        else if (call && m == 12 && e == 604 && w.size() >= 8)
            add(UnlockMemoir, w[4], w[7]);
        else if (call && m == 12 && e == 733 && w.size() >= 8 && w[3] == 0x6923 && w[4] == 1 &&
                 w[5] == 0x2c && w[6] == 0x6923)
            add(Music, w[7]);
        else if (call && m == 5 && e == 1150)
            add(Music, 0, w.size() >= 5 && w[3] == 0x6923 ? w[4] : 1);
        else if (call && m == 7 && e == 2090 && w.size() == 11 && w[9] == 0x6923)
            voice = w[10] - 1;
        else if (op == 0xf801 && w.size() >= 3 && w[1] == 0x6923) {
            auto list = literals(w, 4);
            add(ClearCharacters, int(list.size()));
            int slot = 0;
            for (int x : list)
                add(Character, x, slot++);
        } else if (op == 0xf601 && w.size() >= 3 && w[1] == 0x6923) {
            for (int x : literals(w, 1))
                add(Character, x);
        } else if (op == 0xf701 && w.size() >= 3 && w[1] == 0x6923)
            add(HideCharacter, w[2]);
        else if (op == 0xf901) {
            auto list = literals(w, 1);
            if (list.empty() || list.size() > 2)
                throw std::runtime_error("Invalid background arguments");
            add(Background, list[0], -1, list.size() == 2 ? list[1] : 0);
        } else if (op == 0xfb01) {
            if (w.size() < 3 || w[1] != 0x6923)
                throw std::runtime_error("Invalid speaker");
            add(Speaker, w[2]);
        } else if (op == 0xff01) {
            if (w.size() < 8 || w[1] != 0x6923 || w[3] != 0x2c || w[4] != 0x6923 || w[6] != 0x2c)
                throw std::runtime_error("Invalid dialogue");
            auto j = event(Dialogue, w[2], name);
            j["flags"] = w[5];
            std::string text;
            for (size_t i = 7; i < w.size(); i++) {
                auto token = w[i];
                if (token == 0x23)
                    continue;
                int v = w.at(++i);
                if (token == 0x7323)
                    text += strings.at(v);
                else if (token == 0x7324 && (v == 59 || v == 60))
                    text += v == 59 ? "\xee\x80\x80" : "\xee\x80\x81";
                else
                    throw std::runtime_error("Unsupported dialogue expression");
            }
            j["text"] = text;
            if (w[5] & 0x80) {
                char num[32];
                snprintf(num, sizeof(num), "_%03d.g", ++voice);
                j["voice"] = name.substr(0, name.find_last_of('.')) + num;
            }
            out.push_back(j);
        }
        return out;
    }
    Json compiled() const {
        Json events = Json::object();
        size_t p = 0;
        while (p < code.size()) {
            size_t start = p;
            auto op = code.at(p++);
            if (op == 0x6e46 || op == 0x7b || op == 0x7d || op == 0x7342) {
                p++;
                continue;
            }
            if (op == 0x7254) {
                p += 2;
                continue;
            }
            if (op == 0x7345) {
                p += 1 + code.at(p) * 2;
                continue;
            }
            if (op == 0x203a || op == 0x7452 || op == 0x6c45)
                continue;
            Words w{op};
            if (op == 0x6143) {
                w.push_back(code.at(p++));
                w.push_back(code.at(p++));
            } else if (op == 0x7753)
                p++;
            else if (op == 0x6924)
                p += 2;
            while (p < code.size() && code[p] != 0x203a) {
                auto t = code.at(p++);
                w.push_back(t);
                for (int n = operands(t); n > 0; n--)
                    w.push_back(code.at(p++));
            }
            if (p < code.size())
                p++;
            if ((op & 255) == 1 || op == 0x6143) {
                int voice = 0;
                auto list = statement(w, voice);
                if (!list.empty())
                    events[std::to_string(start)] = std::move(list);
            }
        }
        return {{"name", name}, {"code", code}, {"strings", strings}, {"events", events}};
    }
    Json linear() const {
        if (code.size() < 4 || code[0] != 0x6e46 || code[1] != 0)
            throw std::runtime_error("Missing function zero");
        Json out = Json::array();
        Words w;
        int voice = 0, depth = 0;
        for (size_t p = 2; p < code.size(); p++) {
            auto t = code[p];
            int n = operands(t);
            if (n) {
                w.push_back(t);
                while (n--)
                    w.push_back(code.at(++p));
                continue;
            }
            if (t == 0x7b) {
                depth++;
                p++;
                continue;
            }
            if (t == 0x7d) {
                if (--depth <= 0)
                    break;
                continue;
            }
            if (t == 0x6649 || t == 0x6857 || t == 0x7753 || t == 0x6c45)
                throw std::runtime_error("Branch in linear prologue");
            if (t == 0x7452)
                break;
            if (t == 0x203a) {
                for (auto &e : statement(w, voice))
                    out.push_back(e);
                w.clear();
            } else
                w.push_back(t);
        }
        return out;
    }
};
