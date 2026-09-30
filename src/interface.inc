enum UiScreen {
    UiNone,
    UiSettings,
    UiSave,
    UiHistory,
    UiGallery,
    UiCg,
    UiCgImage,
    UiMusic,
    UiMemoirs,
    UiBonus,
    UiName,
    UiConfirm
};
struct UiButtonSpec {
    SDL_Rect rect;
    int action = 0, value = 0;
    bool enabled = true;
    std::string name;
};
struct UiCached {
    std::unique_ptr<Texture> texture;
    uint64_t used = 0;
    size_t bytes = 0;
};
std::map<std::string, UiCached> uiCache;
std::map<std::string, Bytes> atlasData;
size_t uiBytes = 0;
uint64_t uiCounter = 0;
// Decoded backgrounds and sprites reused by composeScene(): an expression change decodes only the new
// sprite instead of re-reading and re-decompressing every layer of the scene (~0.2 s each on Vita).
struct DecodedImage {
    std::shared_ptr<const Pixels> pixels;
    uint64_t used = 0;
};
std::map<std::string, DecodedImage> decodedImages;
size_t decodedBytes = 0;
uint64_t decodedCounter = 0;
template <class Decode> const Pixels &decodedImage(const std::string &key, Decode decode) {
    auto it = decodedImages.find(key);
    if (it == decodedImages.end()) {
        auto pixels = std::make_shared<const Pixels>(decode());
        size_t size = pixels->rgba.size();
        while (decodedBytes + size > 24u * 1024 * 1024 && !decodedImages.empty()) {
            auto oldest =
                std::min_element(decodedImages.begin(), decodedImages.end(),
                                 [](const auto &a, const auto &b) { return a.second.used < b.second.used; });
            decodedBytes -= oldest->second.pixels->rgba.size();
            decodedImages.erase(oldest);
        }
        decodedBytes += size;
        it = decodedImages.emplace(key, DecodedImage{std::move(pixels), 0}).first;
    }
    it->second.used = ++decodedCounter;
    return *it->second.pixels;
}
std::map<int, TTF_Font *> sizedFonts;
struct GlyphCached {
    std::unique_ptr<Texture> texture;
    uint64_t used = 0;
};
std::map<std::string, GlyphCached> glyphCache;
uint64_t glyphCounter = 0;
// Drop the least recently drawn glyphs instead of the whole cache, so the page being shown never
// has to be rasterized again.
void trimGlyphCache(size_t count) {
    std::vector<std::pair<uint64_t, std::string>> order;
    order.reserve(glyphCache.size());
    for (const auto &g : glyphCache)
        order.emplace_back(g.second.used, g.first);
    count = std::min(count, order.size());
    std::nth_element(order.begin(), order.begin() + count, order.end());
    for (size_t i = 0; i < count; i++)
        glyphCache.erase(order[i].second);
}
std::vector<UiButtonSpec> uiButtons;
UiScreen uiScreen = UiNone, confirmReturn = UiNone;
// Only directional navigation needs a persistent focus indicator.
bool navigationFocus = false, pointerHeld = false;
int pressedAction = -1, pressedValue = 0, draggedSlider = -1;
UiScreen pressedScreen = UiNone;
SDL_Rect pressedRect{}, sliderRect{};
int uiFocus = 0, mouseX = -1, mouseY = -1, settingsTab = 2, savePage = 0, galleryPage = 0, nameField = -1,
    confirmAction = 0, confirmValue = 0;
bool loadingSlots = true, galleryPlaying = false, galleryRepeat = false, galleryShuffle = false,
     skipMode = false, replayMode = false;
std::vector<int> playlist;
int lastTrackClick = 0;
uint32_t lastTrackAt = 0;
std::string galleryImage, confirmQuestion;
Json preferences = {{"effects", true},
                    {"particles", true},
                    {"clock", false},
                    {"instant", false},
                    {"textSpeed", 5},
                    {"autoSpeed", 5},
                    {"readSkip", false},
                    {"skipUnread", false},
                    {"smooth", true},
                    {"smoothFull", true},
                    {"interruptVoice", true},
                    {"master", 20},
                    {"music", 20},
                    {"voice", 20},
                    {"sound", 20},
                    {"vsync", true}};
std::set<std::string> readMessages, unlockedImages;
std::set<int> unlockedMusic, unlockedMemoirs, clearedEndings;
std::vector<std::string> cgNames;
std::vector<Json> historyScenes;
bool uiDirty = false, capturePending = true, haveSaveSlots = false;
uint32_t lastPersist = 0;
NativeMessageLayout message;
float revealed = 0;
bool messageWaiting = true, messageRead = false;
std::string messageKey;
Json messageEvent = nullptr, dialogueAnchor = nullptr;
double skipTimer = 0;
double messageTimer = 0;
int textMode = 1;
OriginalWeather snowfall;
Pixels lastScene, transitionMask;
std::shared_ptr<Pixels> transitionFrame;
Texture effectCover, sceneComposite;
std::string sceneKey;
std::set<std::string> loggedStages;
int effectId = 0, effectStep = 0, effectFrame = -1, effectLength = 0;
uint32_t effectStart = 0;
std::map<int, std::unique_ptr<Texture>> previews;
std::map<int, Json> slotCache;
struct SoundFade {
    int volume = 0, target = 0;
    uint32_t start = 0, duration = 0;
};
std::array<SoundFade, 8> soundFades{};
SoundFade musicFade{}, overlapFade{};
bool suspended = false;
uint32_t suspendedAt = 0;
static int fadeVolume(const SoundFade &fade, uint32_t now) {
    float t = std::clamp((now - fade.start) / float(fade.duration), 0.f, 1.f);
    auto db = [](int volume) { return volume <= 0 ? -80.f : 20.f * std::log10(volume / 128.f); };
    float value = db(fade.volume) + (db(fade.target) - db(fade.volume)) * t;
    return t >= 1 ? fade.target : std::clamp(int(std::lround(128 * std::pow(10.f, value / 20))), 0, 128);
}
void setSuspended(bool value) {
    if (suspended == value)
        return;
    uint32_t now = SDL_GetTicks();
    if (value) {
        suspendedAt = now;
        touchPressed = pointerHeld = false;
        pressedAction = draggedSlider = -1;
        movie.setPaused(true);
        if (audio) {
            Mix_Pause(-1);
            Mix_PauseMusic();
        }
        persistUi();
    } else {
        uint32_t elapsed = now - suspendedAt;
        if (deadline)
            deadline += elapsed;
        effectStart += elapsed;
        splashAt += elapsed;
        endingAt += elapsed;
        musicStartedAt += elapsed;
        for (auto &fade : soundFades)
            if (fade.duration)
                fade.start += elapsed;
        if (musicFade.duration)
            musicFade.start += elapsed;
        if (overlapFade.duration)
            overlapFade.start += elapsed;
        tickAt = now;
        movie.setPaused(false);
        if (audio) {
            Mix_Resume(-1);
            Mix_ResumeMusic();
        }
    }
    suspended = value;
}

void writeState(const std::string &name, const Json &data) {
    std::string p = stateRoot + "/" + name, tmp = p + ".tmp", bak = p + ".bak";
    {
        std::ofstream f(tmp, std::ios::binary);
        f << data.dump();
        f.flush();
        if (!f)
            throw std::runtime_error("Cannot write " + name);
    }
    std::remove(bak.c_str());
    if (exists(p) && std::rename(p.c_str(), bak.c_str()))
        throw std::runtime_error("Cannot back up " + name);
    if (std::rename(tmp.c_str(), p.c_str())) {
        std::rename(bak.c_str(), p.c_str());
        throw std::runtime_error("Cannot commit " + name);
    }
}
void persistUi() {
    if (!uiDirty)
        return;
    writeState("presentation.json", {{"introSeen", introSeen},
                                     {"preferences", preferences},
                                     {"read", readMessages},
                                     {"images", unlockedImages},
                                     {"music", unlockedMusic},
                                     {"memoirs", unlockedMemoirs},
                                     {"endings", clearedEndings}});
    uiDirty = false;
}
void loadUi() {
    auto p = stateRoot + "/presentation.json";
    if (exists(p))
        try {
            auto s = readJson(p);
            introSeen = s.value("introSeen", false);
            if (s.contains("preferences"))
                preferences.update(s["preferences"]);
            readMessages = s.value("read", readMessages);
            unlockedImages = s.value("images", unlockedImages);
            unlockedMusic = s.value("music", unlockedMusic);
            unlockedMemoirs = s.value("memoirs", unlockedMemoirs);
            clearedEndings = s.value("endings", clearedEndings);
        } catch (const std::exception &e) {
            fprintf(stderr, "Presentation settings reset: %s\n", e.what());
        }
    // 0.12-0.14 incorrectly waited for all three linear scripts to finish.
    // Read normal-story lines prove that the Godot first-launch boundary was passed.
    if (!introSeen && std::any_of(readMessages.begin(), readMessages.end(), [](const std::string &key) {
            return key.rfind("com_0110.p:", 0) == 0 || key.rfind("com_0120.p:", 0) == 0;
        })) {
        introSeen = true;
        uiDirty = true;
        persistUi();
        fprintf(stderr, "Migrated intro completion from post-prologue read history\n");
    }
    haveSaveSlots = false;
    for (int n = 1; n <= 40; n++)
        haveSaveSlots |= exists(stateRoot + "/save" + std::to_string(n) + ".json");
    for (const char *key : {"master", "music", "voice", "sound"})
        preferences[key] = std::clamp(preferences.value(key, 20), 0, 20);
    for (const char *key : {"textSpeed", "autoSpeed"})
        preferences[key] = std::clamp(preferences.value(key, 4), 0, 9);
    cgNames = OriginalScript(asset("tenshi_dvd.a"), "%FlagsDef.p").strings;
    applyVolumes();
    SDL_RenderSetVSync(r, preferences.value("vsync", true) ? 1 : 0);
#ifndef __vita__
    if (preferences.value("fullscreen", false))
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
}
void applyVolumes() {
    if (!audio)
        return;
    int master = preferences.at("master");
    Mix_VolumeMusic(master * preferences.at("music").get<int>() * 128 / 400);
    Mix_Volume(0, master * preferences.at("voice").get<int>() * 128 / 400);
    for (int i = 1; i <= 8; i++)
        Mix_Volume(i, master * preferences.at("sound").get<int>() * 128 / 400);
}
TTF_Font *sizedFont(int size) {
    if (usingFullFont) {
        if (!alternateFonts.count(size)) {
            auto *f = openFont("font-alt.ttf", size);
            if (!f)
                throw std::runtime_error(TTF_GetError());
            alternateFonts[size] = f;
        }
        return alternateFonts.at(size);
    }
    if (size == 24)
        return font;
    if (!sizedFonts.count(size)) {
        auto *f = openFont("font.ttf", size);
        if (!f)
            throw std::runtime_error(TTF_GetError());
        sizedFonts[size] = f;
    }
    return sizedFonts.at(size);
}
Bytes &atlas(const std::string &name) {
    if (!atlasData.count(name)) {
        if (atlasData.size() >= 3)
            atlasData.clear();
        atlasData[name] = lzss(asset("sys.a").read(name));
    }
    return atlasData.at(name);
}
int frameCount(const std::string &name) {
    return int(u32(atlas(name), 0));
}
Texture &uiTexture(const std::string &name, int frame) {
    auto key = name + ":" + std::to_string(frame);
    auto it = uiCache.find(key);
    if (it == uiCache.end()) {
        auto pixels = pxFrame(atlas(name), frame);
        size_t size = pixels.rgba.size() * 2;
        while (uiBytes + size > 32u * 1024 * 1024 && !uiCache.empty()) {
            auto oldest = std::min_element(uiCache.begin(), uiCache.end(), [](const auto &a, const auto &b) {
                return a.second.used < b.second.used;
            });
            uiBytes -= oldest->second.bytes;
            uiCache.erase(oldest);
        }
        UiCached cache;
        cache.texture = std::make_unique<Texture>();
        cache.texture->load(r, pixels);
        cache.bytes = size;
        uiBytes += size;
        it = uiCache.emplace(key, std::move(cache)).first;
    }
    it->second.used = ++uiCounter;
    return *it->second.texture;
}
void uiImage(const std::string &name, int frame, int x, int y, int alpha = 255) {
    auto &t = uiTexture(name, frame);
    SDL_SetTextureAlphaMod(t.ptr, uint8_t(std::clamp(alpha, 0, 255)));
    t.draw(r, x, y);
    SDL_SetTextureAlphaMod(t.ptr, 255);
}
void label(const std::string &s, int x, int y, int width = 720, int size = 18, bool center = false,
           SDL_Color color = {255, 255, 255, 255}) {
    if (s.empty())
        return;
    auto *f = sizedFont(size);
    int w = 0, h = 0;
    TTF_SizeUTF8(f, s.c_str(), &w, &h);
    SDL_Rect clip{x, y, width, size * 2 + 8}, old;
    bool clipped = SDL_RenderIsClipEnabled(r);
    SDL_RenderGetClipRect(r, &old);
    SDL_RenderSetClipRect(r, &clip);
    auto key = "label:" + s + ":" + std::to_string(size) + ":" + std::to_string(color.r) + ":" +
               std::to_string(color.g) + ":" + std::to_string(color.b);
    if (!textCache.count(key)) {
        if (textCache.size() > 180)
            textCache.clear();
        auto *surface = TTF_RenderUTF8_Blended(f, s.c_str(), color);
        if (surface) {
            auto t = std::make_unique<Texture>();
            t->loadSurface(r, surface);
            SDL_FreeSurface(surface);
            textCache[key] = std::move(t);
        }
    }
    if (textCache.count(key))
        textCache[key]->draw(r, x + (center ? std::max(0, (width - w) / 2) : 0), y);
    SDL_RenderSetClipRect(r, clipped ? &old : nullptr);
}
// A compact pointer keeps pad navigation visible without drawing a frame around original UI art.
void navigationPointer(SDL_Rect rect) {
    int x = rect.x >= 12 ? rect.x - 10 : rect.x + rect.w + 4;
    if (x + 6 > 800)
        x = rect.x + 4;
    int y = rect.y + rect.h / 2;
    for (int i = 0; i < 6; ++i)
        rectangle(x + i, y - 5 + i, 1, 11 - i * 2, {255, 255, 255, 255});
}
void button(const std::string &name, int frame, int hot, int x, int y, int action, int value = 0,
            bool enabled = true, bool selected = true, int pressed = -1, int disabled = -1) {
    auto &normal = uiTexture(name, frame);
    SDL_Rect rect{x, y, normal.w, normal.h};
    int index = int(uiButtons.size());
    bool focus = navigationFocus && index == uiFocus;
    SDL_Point mouse{mouseX, mouseY};
    bool over = SDL_PointInRect(&mouse, &rect);
    bool down =
        pointerHeld && pressedAction == action && pressedValue == value && pressedScreen == uiScreen && over;
    int drawFrame = !enabled && disabled >= 0                ? disabled
                    : down && pressed >= 0                   ? pressed
                    : enabled && hot >= 0 && (focus || over) ? hot
                                                             : frame;
    auto &t = uiTexture(name, drawFrame);
    uint8_t tint = !enabled ? (disabled >= 0 ? 255 : 70) : (selected ? 255 : 140);
    SDL_SetTextureColorMod(t.ptr, tint, tint, tint);
    t.draw(r, x, y);
    SDL_SetTextureColorMod(t.ptr, 255, 255, 255);
    if (focus && enabled && hot < 0)
        navigationPointer(rect);
    uiButtons.push_back({rect, action, value, enabled, name});
}
void showUi(UiScreen next) {
    pointerHeld = false;
    pressedAction = draggedSlider = -1;
    uiScreen = next;
    uiFocus = 0;
    mouseX = mouseY = -1;
    uiButtons.clear();
    messageTimer = 0;
    if (next != UiName) {
        nameField = -1;
        SDL_StopTextInput();
    }
    if (next == UiSave) {
        previews.clear();
        slotCache.clear();
    }
}
void closeUi() {
    bool wasGallery = uiScreen >= UiGallery && uiScreen <= UiBonus;
    showUi(UiNone);
    persistUi();
    capturePending = true;
    if (wasGallery) {
        galleryPlaying = false;
        stopVoice();
        stopMusic();
        playMusic(24);
    }
}
void ask(const std::string &q, int action, int value = 0) {
    confirmQuestion = q;
    confirmAction = action;
    confirmValue = value;
    confirmReturn = uiScreen;
    showUi(UiConfirm);
}
std::string speakerName(const Json &v) {
    int id = v.value("speaker", 255);
    if (id == 0)
        return surname;
    int at = id * 2 + 1;
    return id != 255 && at >= 0 && at < int(vm->metadata().at("characters").size())
               ? vm->metadata().at("characters").at(at).get<std::string>()
               : "";
}
std::string backgroundName(int id) {
    int v = id & 0x7fff;
    const auto &chars = vm->metadata().at("characters");
    return v < 0x1000 ? "bg" + number(v) + ".px"
                      : "eg" + chars.at((v >> 12) * 2).get<std::string>() + number((v >> 4) & 255, 2) +
                            number(v & 15, 1) + ".px";
}
Pixels composeScene(const Json &v) {
    Pixels out(800, 600);
    for (size_t p = 0; p < out.rgba.size(); p += 4) {
        out.rgba[p] = 5;
        out.rgba[p + 1] = 6;
        out.rgba[p + 2] = 8;
        out.rgba[p + 3] = 255;
    }
    if (v.value("background", -1) >= 0) {
        auto name = backgroundName(v.at("background"));
        blit(out, decodedImage("egbg.a/" + name, [&] { return background(asset("egbg.a"), name); }), 0, 0);
    }
    int count = v.value("count", 0), centers[] = {150, 400, 650};
    if (count == 1)
        centers[0] = 399;
    else if (count == 2) {
        centers[0] = 209;
        centers[1] = 589;
    }
    if (!v.value("full", false))
        for (int i = 0; i < std::min(3, count); i++) {
            int code = v.at("characters").at(i);
            if (!code)
                continue;
            std::string file = vm->metadata().at("characters").at(((code >> 8) & 15) * 2).get<std::string>() +
                               number((code & 15) * 100 + ((code >> 4) & 15)) + ".px";
            const auto &p =
                decodedImage("char.a/" + file, [&] { return pxStandalone(asset("char.a").read(file)); });
            blit(out, p, centers[i] + p.x, 599 + p.y);
        }
    sceneTone(out, v.value("tone", 0));
    return out;
}
void drawSceneBase(const Json &v) {
    auto key =
        Json::array({v.at("background"), v.at("characters"), v.at("count"), v.at("full"), v.value("tone", 0)})
            .dump();
    if (key != sceneKey) {
        uint32_t started = SDL_GetTicks();
        sceneComposite.load(r, composeScene(v));
        sceneKey = key;
        capturePending = true;
        if (SDL_GetTicks() - started >= 50)
            perfLog("scene compose", SDL_GetTicks() - started);
    }
    sceneComposite.draw(r, 0, 0);
}
void drawMessage(const NativeMessageLayout &layout, int at, float distance, int ox, int oy, bool smooth) {
    if (at < 0 || at >= int(layout.pages.size()))
        return;
    usingFullFont = preferences.value(layout.full ? "alternateFull" : "alternateNormal", false) &&
                    exists(stateRoot + "/font-alt.ttf");
    SDL_Rect clip{ox, oy, layout.full ? 654 : 494, layout.full ? 520 : 90}, old;
    bool clipped = SDL_RenderIsClipEnabled(r);
    SDL_RenderGetClipRect(r, &old);
    SDL_RenderSetClipRect(r, &clip);
    for (const auto &g : layout.pages[at]) {
        float opacity = g.advance > 0 ? std::clamp((distance - g.offset) / g.advance, 0.f, 1.f) : 1;
        if (opacity <= 0)
            break;
        if (g.text == " " || g.text == "　")
            continue;
        auto key = std::to_string(g.size) + (usingFullFont ? ":alt:" : ":normal:") +
                   (smooth ? ":s:" : ":b:") + g.text;
        auto cached = glyphCache.find(key);
        if (cached == glyphCache.end()) {
            if (glyphCache.size() >= 3072)
                trimGlyphCache(1024);
            auto *f = sizedFont(g.size);
            auto *surface = smooth ? TTF_RenderUTF8_Blended(f, g.text.c_str(), {255, 255, 255, 255})
                                   : TTF_RenderUTF8_Solid(f, g.text.c_str(), {255, 255, 255, 255});
            if (!surface)
                continue;
            auto t = std::make_unique<Texture>();
            t->loadSurface(r, surface);
            SDL_FreeSurface(surface);
            cached = glyphCache.emplace(key, GlyphCached{std::move(t), 0}).first;
        }
        cached->second.used = ++glyphCounter;
        auto &t = *cached->second.texture;
        SDL_SetTextureColorMod(t.ptr, 0, 0, 0);
        SDL_SetTextureAlphaMod(t.ptr, uint8_t(opacity * glyphOpacity * 255 * .7f));
        t.draw(r, ox + int(g.x) + 1, oy + int(g.y) - TTF_FontAscent(sizedFont(g.size)) + 1);
        SDL_SetTextureColorMod(t.ptr, 255, 255, 255);
        SDL_SetTextureAlphaMod(t.ptr, uint8_t(opacity * glyphOpacity * 255));
        t.draw(r, ox + int(g.x), oy + int(g.y) - TTF_FontAscent(sizedFont(g.size)));
    }
    SDL_RenderSetClipRect(r, clipped ? &old : nullptr);
    usingFullFont = false;
}
void dialogueUi(const Json &v, const NativeMessageLayout &layout, int at, float distance, bool controls) {
    if (!v.value("window", true))
        return;
    bool full = v.value("full", false);
    if (full) {
        uiImage("common.px", 106, 0, 0);
        rectangle(56, 0, 676, 600, {0, 34, 70, 126});
    } else {
        uiImage("common.px", 0, 0, 497);
        auto name = speakerName(v);
        if (!name.empty()) {
            uiImage("common.px", 8, 0, 462);
            label(name, 96, 468, 170, 24, true);
        }
    }
    drawMessage(layout, at, distance, full ? 74 : 154, full ? 20 : 504,
                preferences.value(full ? "smoothFull" : "smooth", true));
    if (controls) {
        button("common.px", 16, 17, 670, 561, 20, 0, true, true, 18, 19);
        button("common.px", 21, 22, 696, 535, 21, 0, true, autoMode, 23, 24);
        button("common.px", 26, 27, 722, 509, 2, 0, true, true, 28, 29);
    }
}
void drawWeather() {
    if (!preferences.value("effects", true) || !preferences.value("particles", true) || !snowfall.flags)
        return;
    for (int i = 0; i < snowfall.density; i++) {
        auto &p = snowfall.particles[i];
        if (!p.started || p.delay > 0 || p.frame < 0 || p.frame >= frameCount("snow.px"))
            continue;
        float x = p.x, y = p.y;
        if (snowfall.type == 2 || snowfall.type == 6) {
            float f = (snowfall.type == 6 ? 180000 + p.z * 1800 : 280000 + p.z * 1000) / (300 * 4096.f);
            x = 400 + x * f;
            y = 300 + y * f;
        }
        auto &t = uiTexture("snow.px", p.frame);
        SDL_SetTextureAlphaMod(t.ptr, uint8_t(p.alpha * 255));
        t.draw(r, int(x) + t.originX, int(y) + t.originY);
        SDL_SetTextureAlphaMod(t.ptr, 255);
    }
}
void drawClock() {
    if (!preferences.value("clock", false))
        return;
    uiImage("common.px", 64, 751, 577);
    std::time_t now = std::time(nullptr);
    auto t = *std::localtime(&now);
    auto digits = [&](int v, int x, int y) {
        uiImage("common.px", 45 + v / 10, 751 + x, 577 + y);
        uiImage("common.px", 45 + v % 10, 756 + x, 577 + y);
    };
    digits(t.tm_hour, 11, 3);
    digits(t.tm_min, 29, 3);
    digits(t.tm_mon + 1, 11, 12);
    digits(t.tm_mday, 29, 12);
}
void backdrop() {
    rectangle(0, 0, 800, 600, {0, 0, 0, 115});
    uiImage("system.px", 40, 0, 0);
    uiImage("system.px", 41, 0, 544);
}
void slider(int x, int y, int count, int value, int index) {
    uiImage("system.px", count == 10 ? 44 : 45, x, y);
    for (int i = 0; i < value; i++)
        uiImage("system.px", 39, x + i * 12, y);
    int n = int(uiButtons.size());
    SDL_Rect box{x, y, count == 10 ? 118 : 238, 30};
    if (navigationFocus && n == uiFocus)
        navigationPointer(box);
    uiButtons.push_back({box, 30, index, true, "slider"});
}
void pair(int a, int b, int x1, int y1, int x2, int y2, int setting, bool value) {
    bool enabled = true;
#ifdef __vita__
    if (setting == 3) {
        enabled = false;
        value = true;
    }
#endif
    button("system.px", a, -1, x1, y1, 31, setting * 2 + 1, enabled, value);
    button("system.px", b, -1, x2, y2, 31, setting * 2, enabled, !value);
}

void settingsUi() {
    backdrop();
    uiImage("system.px", settingsTab, 120, 120);
    int xs[] = {120, 0, 720, 120}, ys[] = {43, 120, 120, 496};
    for (int i = 0; i < 4; i++)
        button("system.px", 4 + i, -1, xs[i], ys[i], 2, i, true, settingsTab == i);
    if (settingsTab == 0) {
        for (int i = 0; i < 3; i++)
            uiImage("system.px", 46 + i, 160, 137 + 58 * i);
        pair(33, 32, 480, 147, 372, 147, 0, preferences.value("readSkip", false));
        pair(34, 35, 289, 205, 397, 205, 1, preferences.value("instant", false));
        slider(502, 205, 10, preferences.at("textSpeed").get<int>() + 1, 0);
        pair(37, 36, 397, 263, 289, 263, 2, autoMode);
        slider(502, 263, 10, preferences.at("autoSpeed").get<int>() + 1, 1);
        button("system.px", 49, -1, 160, 311, 32, 0);
        button("system.px", 51, -1, 160, 369, 32, 1);
        label(preferences.value("alternateNormal", false) ? "备用字体" : "思源黑体（内置）", 290, 320, 180);
        label(preferences.value("alternateFull", false) ? "备用字体" : "思源黑体（内置）", 290, 377, 180);
        button("system.px", 50, -1, 513, 311, 33, 0);
        button("system.px", 50, -1, 513, 369, 33, 1);
        if (preferences.value("smooth", true))
            uiImage("system.px", 38, 587, 321);
        if (preferences.value("smoothFull", true))
            uiImage("system.px", 38, 587, 379);
    } else if (settingsTab == 1) {
        button("system.px", 8, -1, 240, 160, 3, 0, inGame && !dialogueAnchor.is_null());
        button("system.px", 9, -1, 390, 160, 3, 1);
        button("system.px", 10, -1, 240, 310, 40);
        button("system.px", 11, -1, 390, 310, 41);
    } else if (settingsTab == 2) {
        for (int i = 0; i < 5; i++)
            uiImage("system.px", 12 + i, 200, 160 + i * 58);
        pair(22, 23, 370, 170, 478, 170, 3, preferences.value("fullscreen", false));
        pair(24, 25, 370, 228, 478, 228, 4, preferences.value("effects", true));
        pair(27, 26, 478, 286, 370, 286, 5, preferences.value("vsync", true));
        pair(28, 29, 370, 344, 478, 344, 6, preferences.value("particles", true));
        pair(30, 31, 370, 402, 478, 402, 7, preferences.value("clock", false));
    } else {
        for (int i = 0; i < 5; i++)
            uiImage("system.px", 17 + i, 200, 178 + i * 58);
        const char *keys[] = {"master", "music", "voice", "sound"};
        for (int i = 0; i < 4; i++)
            slider(318, 188 + 58 * i, 20, preferences.at(keys[i]), 2 + i);
        pair(24, 25, 362, 420, 470, 420, 8, !preferences.value("interruptVoice", true));
    }
}
static void validateSaveVisual(const Json &v) {
    for (const char *key : {"background", "count", "speaker", "music"})
        v.at(key).get<int>();
    for (const char *key : {"text", "voice"})
        v.at(key).get<std::string>();
    for (const char *key : {"characters", "previous"}) {
        if (!v.at(key).is_array() || v.at(key).size() != 3)
            throw std::runtime_error("Invalid character slots");
        for (const auto &item : v.at(key))
            item.get<int>();
    }
    for (const char *key : {"full", "window", "clear"})
        v.at(key).get<bool>();
    if (!v.at("choice").is_array())
        throw std::runtime_error("Invalid saved choices");
    for (const auto &item : v.at("choice"))
        item.get<std::string>();
    v.value("tone", 0);
    v.value("weather", 0);
}
Json slotInfo(int n) {
    if (slotCache.count(n))
        return slotCache.at(n);
    auto p = stateRoot + "/save" + std::to_string(n) + ".json";
    if (!exists(p))
        return slotCache[n] = nullptr;
    try {
        auto record = readJson(p);
        if (record.at("format") != 1)
            throw std::runtime_error("Unknown save format");
        validateSaveVisual(record.at("visual"));
        record.value("surname", std::string());
        record.value("given", std::string());
        record.value("savedAt", std::string());
        record.value("page", 0);
        return slotCache[n] = std::move(record);
    } catch (const std::exception &e) {
        fprintf(stderr, "Unreadable save %d: %s\n", n, e.what());
        return slotCache[n] = nullptr;
    }
}
void saveUi() {
    backdrop();
    uiImage("saveload.px", 0, 11, 69);
    uiImage("saveload.px", loadingSlots ? 3 : 2, 20, 94);
    for (int i = 0; i < 8; i++) {
        int n = savePage * 8 + i + 1, x = 64 + i / 4 * 370, y = 81 + i % 4 * 114;
        auto record = slotInfo(n);
        button("saveload.px", 1, -1, x, y, 4, n, !loadingSlots || !record.is_null());
        label(number(n, 2), x + 6, y + 5, 60, 16);
        if (record.is_null())
            label("NO DATA", x + 16, y + 40, 200);
        else {
            auto &v = record.at("visual");
            if (!previews.count(n)) {
                auto tex = std::make_unique<Texture>();
                tex->ptr = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 800, 600);
                tex->w = 800;
                tex->h = 600;
                if (!tex->ptr)
                    throw std::runtime_error(SDL_GetError());
                auto *target = SDL_GetRenderTarget(r);
                SDL_SetRenderTarget(r, tex->ptr);
                snapshots.begin(tex->ptr);
                drawSceneBase(v);
                auto oldSurname = surname, oldGiven = given;
                surname = record.value("surname", surname);
                given = record.value("given", given);
                NativeMessageLayout layout;
                layout.set(formatted(v.at("text")), v.value("full", false));
                dialogueUi(v, layout, std::clamp(record.value("page", 0), 0, int(layout.pages.size()) - 1),
                           1000000, false);
                surname = oldSurname;
                given = oldGiven;
                tex->nested = snapshots.finishNestedFrame();
                SDL_SetRenderTarget(r, target);
                previews[n] = std::move(tex);
            }
            previews[n]->draw(r, x + 228, y + 9, 114, 85);
            label(record.value("savedAt", std::string("旧版存档")), x + 16, y + 32, 210);
            label(v.value("speaker", 255) == 0 ? record.value("surname", surname) : speakerName(v), x + 4,
                  y + 54, 220, 16);
            auto text = formatted(v.value("text", std::string()));
            std::replace(text.begin(), text.end(), '\x7f', ' ');
            label(text, x + 4, y + 80, 220, 16);
        }
    }
    for (int i = 0; i < 5; i++)
        button("saveload.px", 4 + i * 2 + (i == savePage ? 1 : 0), -1, 22, 320 + i * 40, 5, i);
}
void historyUi() {
    if (historyScenes.empty()) {
        closeUi();
        return;
    }
    historyAt = std::clamp(historyAt, 0, int(historyScenes.size()) - 1);
    auto &v = historyScenes[historyAt];
    drawSceneBase(v);
    NativeMessageLayout layout;
    layout.set(formatted(v.at("text")), v.value("full", false));
    dialogueUi(v, layout, 0, 1000000, false);
    button("common.px", 91, 92, 670, 561, 22, -1);
    button("common.px", 96, 97, 696, 535, 23, 0, !v.value("voice", std::string()).empty());
    button("common.px", 101, 102, 722, 509, 24);
    label(std::to_string(historyAt + 1) + "/" + std::to_string(historyScenes.size()), 18, 505, 115, 16);
}
void galleryBoard(int heading) {
    backdrop();
    uiImage("omake.px", 0, 11, 69);
    uiImage("omake.px", heading, 20, 94);
}
void galleryUi() {
    if (uiScreen == UiGallery) {
        int frames[] = {18, 22, 20};
        for (int i = 0; i < 3; i++)
            button("title.px", frames[i], frames[i] - 1, 310, 377 + i * 43, 50, i);
        if (clearedEndings.size() >= 5)
            button("title.px", 24, 23, 310, 506, 50, 3);
    } else if (uiScreen == UiCg) {
        galleryBoard(2);
        std::string atlasName = "thumbnail" + std::to_string(galleryPage) + ".px";
        int n = frameCount(atlasName) / 2;
        for (int i = 0; i < n; i++) {
            int index = galleryPage * 30 + i;
            std::string name = index < int(cgNames.size()) ? "eg" + cgNames[index] + ".px" : "";
            bool unlocked = unlockedImages.count(lower(name));
            button(atlasName, i * 2 + (unlocked ? 0 : 1), -1, 68 + i % 6 * 120, 90 + i / 6 * 88, 51, index,
                   unlocked);
        }
        for (int i = 0; i < 5; i++)
            button("omake.px", 12 + 2 * i + (i == galleryPage ? 1 : 0), -1, 22, 320 + 40 * i, 52, i);
    } else if (uiScreen == UiCgImage) {
        auto key = "CG:" + galleryImage;
        if (sceneKey != key) {
            sceneComposite.load(r, background(asset("egbg.a"), galleryImage));
            sceneKey = key;
        }
        sceneComposite.draw(r, 0, 0, 800, 600);
        uiButtons.push_back({{0, 0, 800, 600}, 53, 0, true, "cg"});
    } else if (uiScreen == UiMemoirs) {
        galleryBoard(1);
        int n = frameCount("memoirs.px") / 2;
        for (int i = 0; i < n; i++) {
            bool unlocked = unlockedMemoirs.count(33 + i);
            button("memoirs.px", i * 2 + (unlocked ? 0 : 1), -1, 68 + i % 6 * 120, 90 + i / 6 * 88, 54, i,
                   unlocked);
        }
    } else if (uiScreen == UiBonus) {
        galleryBoard(4);
        for (int i = 0; i < 6; i++) {
            button("omake.px", 26, -1, 230, 105 + i * 60, 55, i);
            uiImage("omake.px", 54 + i, 234, 110 + i * 60);
        }
    } else if (uiScreen == UiMusic) {
        galleryBoard(3);
        for (int i = 0; i < 24; i++) {
            int track = i + 1, x = 74 + i / 12 * 362, y = 74 + i % 12 * 38;
            bool unlocked = track == 24 || unlockedMusic.count(track);
            button("omake.px", 26, -1, x, y, 56, track, unlocked);
            uiImage("omake.px", 28 + i, x + 40, y + 5, unlocked ? 255 : 51);
            if (std::find(playlist.begin(), playlist.end(), track) != playlist.end())
                uiImage("omake.px", 25, x + 9, y + 6);
        }
        button("omake.px", 6, -1, 28, 320, 57);
        button("omake.px", 8, -1, 28, 380, 58, 0, true, galleryRepeat);
        button("omake.px", 10, -1, 28, 440, 59, 0, true, galleryShuffle);
    }
}
Texture canvas;
bool canvasReady = false;
uint32_t tickAt = 0;
bool heldSkip = false;
std::map<int, TTF_Font *> alternateFonts;
bool usingFullFont = false;
void returnTitle() {
    effectId = 0;
    deadline = 0;
    ending = false;
    inGame = false;
    menu = true;
    replayMode = false;
    hidden = false;
    galleryPlaying = false;
    autoMode = skipMode = false;
    stopVoice();
    stopSounds();
    stopMusic();
    musicId = -1;
    playMusic(24);
    closeUi();
}
void nameUi() {
    if (!nameBackground.ptr) {
        auto p = pxFrame(atlas("title.px"), 5);
        for (size_t i = 0; i < p.rgba.size(); i += 4)
            for (int c = 0; c < 3; c++)
                p.rgba[i + c] = uint8_t(std::max(0, int(p.rgba[i + c]) - 112));
        nameBackground.load(r, p);
    }
    nameBackground.draw(r, 0, 0);
    uiImage("system.px", 40, 0, 0);
    uiImage("system.px", 41, 0, 544);
    uiImage("inputName.px", 0, 105, 109);
    for (int i = 0; i < 2; i++) {
        SDL_Rect box{i ? 412 : 188, 192, 200, 49};
        const auto &value = i ? given : surname;
        label(value, box.x + 8, box.y + 9, 184, 25, false, {20, 43, 43, 255});
        if (nameField == i) {
            int width = 0, height = 0;
            TTF_SizeUTF8(sizedFont(25), value.c_str(), &width, &height);
            rectangle(box.x + 8 + std::clamp(width, 0, 182), box.y + 11, 2, 27, {20, 43, 43, 255});
        } else if (navigationFocus && int(uiButtons.size()) == uiFocus)
            navigationPointer(box);
        uiButtons.push_back({box, 60, i, true, "name"});
    }
    button("inputName.px", 1, -1, 420, 262, 61);
    button("inputName.px", 2, -1, 251, 262, 62);
}
void confirmUi() {
    if (confirmReturn == UiName) {
        nameUi();
        uiButtons.clear();
        // The name screen underneath only changes with these inputs; shade it once instead of
        // rasterizing the whole frame on the CPU and creating a texture every frame.
        auto key = surname + '\x1f' + given + '\x1f' + std::to_string(nameField) + '\x1f' +
                   std::to_string(navigationFocus ? uiFocus : -1);
        if (!confirmShade.ptr || confirmShadeKey != key) {
            Pixels p = snapshots.current();
            for (size_t i = 0; i < p.rgba.size(); i += 4) {
                int g = std::min(255, std::max(0, int(p.rgba[i + 1]) - 112) + 128);
                p.rgba[i] = p.rgba[i + 1] = p.rgba[i + 2] = uint8_t(g);
            }
            confirmShade.load(r, std::move(p));
            confirmShadeKey = key;
        }
        confirmShade.draw(r, 0, 0);
    } else
        backdrop();
    uiImage("common.px", 68, 105, 174);
    label(confirmQuestion, 150, 201, 500, 24, true, {255, 255, 255, 255});
    button("common.px", 69, -1, 251, 262, 63);
    button("common.px", 70, -1, 420, 262, 64);
}
void playGalleryTrack(int id) {
    galleryPlaying = true;
    musicId = -1;
    playMusic(id, -1, false);
}
void changeSlider(int setting, int delta, int absolute = -1) {
    const char *keys[] = {"textSpeed", "autoSpeed", "master", "music", "voice", "sound"};
    if (setting < 0 || setting > 5)
        return;
    int max = setting < 2 ? 9 : 20;
    preferences[keys[setting]] =
        std::clamp(absolute >= 0 ? absolute : preferences.at(keys[setting]).get<int>() + delta, 0, max);
    uiDirty = true;
    applyVolumes();
}
void dispatch(int a, int v = 0) {
    switch (a) {
    case 1:
        showUi(UiName);
        break;
    case 2:
        settingsTab = v;
        showUi(UiSettings);
        break;
    case 3:
        loadingSlots = v != 0;
        showUi(UiSave);
        break;
    case 4:
        slot = v;
        if (loadingSlots) {
            loadGame();
        } else if (!slotInfo(v).is_null())
            ask("覆盖这个存档？", 72, v);
        else {
            saveGame();
            previews.clear();
            slotCache.clear();
        }
        break;
    case 5:
        savePage = v;
        uiFocus = 0;
        previews.clear();
        slotCache.clear();
        break;
    case 6:
        showUi(UiGallery);
        break;
    case 7:
        ask("退出游戏？", 73);
        break;
    case 20:
        if (!historyScenes.empty()) {
            autoMode = skipMode = false;
            historyAt = int(historyScenes.size()) - 1;
            showUi(UiHistory);
        }
        break;
    case 21:
        autoMode = !autoMode;
        skipMode = false;
        messageTimer = 0;
        break;
    case 22:
        if (historyAt + v >= int(historyScenes.size()))
            closeUi();
        else
            historyAt = std::max(0, historyAt + v);
        break;
    case 23:
        if (!historyScenes.empty())
            playVoice(historyScenes.at(historyAt).value("voice", std::string()));
        break;
    case 24:
        closeUi();
        break;
    case 30:
        changeSlider(v, 1);
        break;
    case 31: {
        int setting = v / 2;
        bool value = v % 2;
        const char *keys[] = {"readSkip", "instant",   "auto",  "fullscreen",    "effects",
                              "vsync",    "particles", "clock", "interruptVoice"};
        if (setting == 2) {
            autoMode = value;
            skipMode = false;
        } else if (setting == 3) {
#ifndef __vita__
            SDL_SetWindowFullscreen(window, value ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
#endif
            preferences["fullscreen"] = value;
        } else {
            preferences[keys[setting]] = setting == 8 ? !value : value;
            if (setting == 0)
                skipMode = value && messageRead;
            if (setting == 5)
                SDL_RenderSetVSync(r, value ? 1 : 0);
        }
        uiDirty = true;
        break;
    }
    case 32: {
        const char *key = v ? "alternateFull" : "alternateNormal";
        if (!exists(stateRoot + "/font-alt.ttf")) {
            notice = "请将备用字体放入数据目录：font-alt.ttf";
            break;
        }
        auto *test = openFont("font-alt.ttf", 24);
        if (!test) {
            notice = "font-alt.ttf 无法加载";
            break;
        }
        TTF_CloseFont(test);
        preferences[key] = !preferences.value(key, false);
        glyphCache.clear();
        uiDirty = true;
        break;
    }
    case 33: {
        const char *key = v ? "smoothFull" : "smooth";
        preferences[key] = !preferences.value(key, true);
        glyphCache.clear();
        uiDirty = true;
        break;
    }
    case 40:
        ask("返回标题画面？", 74);
        break;
    case 41:
        ask("退出游戏？", 73);
        break;
    case 50:
        showUi(v == 0 ? UiCg : v == 1 ? UiMemoirs : v == 2 ? UiMusic : UiBonus);
        break;
    case 51:
        if (v >= 0 && v < int(cgNames.size())) {
            galleryImage = "eg" + cgNames[v] + ".px";
            showUi(UiCgImage);
        }
        break;
    case 52:
        galleryPage = v;
        uiFocus = 0;
        break;
    case 53:
        showUi(UiCg);
        break;
    case 54: {
        resetStory();
        vm->startReplay(v);
        replayMode = true;
        advance();
        break;
    }
    case 55: {
        static const char *names[] = {"toh", "sin", "asu", "yuk", "mah", "emi"};
        if (v >= 0 && v < 6)
            playVoice(std::string("!") + names[v] + "_bonus.g");
        break;
    }
    case 56: {
        uint32_t now = SDL_GetTicks();
        if (v == lastTrackClick && now - lastTrackAt <= 500) {
            if (std::find(playlist.begin(), playlist.end(), v) == playlist.end())
                playlist.push_back(v);
            playGalleryTrack(v);
        } else {
            auto it = std::find(playlist.begin(), playlist.end(), v);
            if (it == playlist.end())
                playlist.push_back(v);
            else
                playlist.erase(it);
        }
        lastTrackClick = v;
        lastTrackAt = now;
        break;
    }
    case 57:
        if (galleryPlaying) {
            galleryPlaying = false;
            stopMusic();
        } else if (!playlist.empty())
            playGalleryTrack(playlist.front());
        break;
    case 58:
        galleryRepeat = !galleryRepeat;
        break;
    case 59:
        galleryShuffle = !galleryShuffle;
        break;
    case 60:
        nameField = v;
        SDL_StartTextInput();
        break;
    case 61:
        surname = "木田";
        given = "时纪";
        break;
    case 62:
        if (surname.empty() || given.empty()) {
            notice = "姓名不能为空";
            break;
        }
        ask(surname + given + "这样可以吗？", 70);
        break;
    case 63: {
        int action = confirmAction, value = confirmValue;
        showUi(confirmReturn);
        dispatch(action, value);
        break;
    }
    case 64:
        if (confirmReturn == UiName)
            showUi(UiName);
        else
            closeUi();
        break;
    case 70:
        writeState("player.json", {{"surname", surname}, {"given", given}});
        newGame();
        break;
    case 71:
        slot = v;
        loadGame();
        break;
    case 72:
        slot = v;
        saveGame();
        showUi(UiSave);
        break;
    case 73:
        persistUi();
        running = false;
        break;
    case 74:
        returnTitle();
        break;
    case 80:
        choice = v;
        vm->choose(choice + 1);
        visual["choice"] = Json::array();
        advance();
        break;
    }
}
void goBack() {
    notice.clear();
    if (uiScreen == UiName && nameField >= 0) {
        nameField = -1;
        SDL_StopTextInput();
        return;
    }
    if (uiScreen == UiConfirm && confirmReturn == UiName) {
        showUi(UiName);
        return;
    }

    closeUi();
}
void pointer(int x, int y) {
    navigationFocus = false;
    if (!fatal.empty() || splash || movie.active() || ending) {
        action(1);
        return;
    }
    for (size_t i = 0; i < uiButtons.size(); i++) {
        auto b = uiButtons[i];
        SDL_Point p{x, y};
        if (b.enabled && SDL_PointInRect(&p, &b.rect)) {
            uiFocus = int(i);
            if (b.action == 30) {
                int count = b.value < 2 ? 10 : 20;
                int value = (x - b.rect.x) * count / b.rect.w + 1;
                changeSlider(b.value, 0, value - (b.value < 2 ? 1 : 0));
            } else
                dispatch(b.action, b.value);
            return;
        }
    }
    if (!menu && uiScreen == UiNone)
        action(1);
}
void dragSlider(int x) {
    if (draggedSlider < 0)
        return;
    int count = draggedSlider < 2 ? 10 : 20;
    int value = (x - sliderRect.x) * count / sliderRect.w + 1;
    value = std::clamp(value, draggedSlider < 2 ? 1 : 0, count);
    changeSlider(draggedSlider, 0, value - (draggedSlider < 2 ? 1 : 0));
}
void pointerDown(int x, int y) {
    navigationFocus = false;
    if (pointerHeld)
        return;
    pointerHeld = true;
    pressedAction = draggedSlider = -1;
    SDL_Point point{x, y};
    for (size_t i = 0; i < uiButtons.size(); ++i) {
        const auto &b = uiButtons[i];
        if (!b.enabled || !SDL_PointInRect(&point, &b.rect))
            continue;
        uiFocus = int(i);
        if (b.action == 30) {
            draggedSlider = b.value;
            sliderRect = b.rect;
            dragSlider(x);
        } else {
            pressedAction = b.action;
            pressedValue = b.value;
            pressedRect = b.rect;
            pressedScreen = uiScreen;
        }
        return;
    }
    pointer(x, y); // Story-page clicks still complete/advance on press, as in Godot.
}
void pointerUp(int x, int y) {
    int a = pressedAction, v = pressedValue;
    bool held = pointerHeld;
    pointerHeld = false;
    pressedAction = draggedSlider = -1;
    SDL_Point point{x, y};
    if (held && a >= 0 && uiScreen == pressedScreen && SDL_PointInRect(&point, &pressedRect))
        dispatch(a, v);
}
bool touchPressed = false;
SDL_TouchID activeTouch = 0;
SDL_FingerID activeFinger = 0;
unsigned touchLogCount = 0;
#ifdef __vita__
static constexpr bool frontTouchOnly = true;
#else
static constexpr bool frontTouchOnly = false;
#endif
bool pointerEvent(const SDL_Event &e, bool frontOnly = frontTouchOnly) {
    const auto windowId = SDL_GetWindowID(window);
    if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION || e.type == SDL_FINGERUP) {
        const auto &f = e.tfinger;
        // SDL's Vita driver identifies the front panel as 1 and the rear panel as 2.
        if (f.touchId == SDL_MOUSE_TOUCHID || (frontOnly && f.touchId != 1) ||
            (f.windowID && f.windowID != windowId))
            return true;
        if (e.type == SDL_FINGERDOWN) {
            if (touchPressed)
                return true;
            touchPressed = true;
            activeTouch = f.touchId;
            activeFinger = f.fingerId;
        } else if (!touchPressed || f.touchId != activeTouch || f.fingerId != activeFinger)
            return true;
        if (e.type == SDL_FINGERUP) {
            int x = std::isfinite(f.x) && f.x > 0 && f.x < 1 ? int(f.x * 800) : -1;
            int y = std::isfinite(f.y) && f.y > 0 && f.y < 1 ? int(f.y * 600) : -1;
            pointerUp(x, y);
            touchPressed = false;
            mouseX = mouseY = -1;
            return true;
        }
        // SDL_RenderSetLogicalSize already adjusts normalized finger positions for
        // the letterboxed canvas. Scaling from 960x544 again would offset every hit.
        // SDL clamps touches in the black bars to 0/1; don't turn those into clicks.
        if (!std::isfinite(f.x) || !std::isfinite(f.y) || f.x <= 0 || f.x >= 1 || f.y <= 0 || f.y >= 1) {
            mouseX = mouseY = -1;
            return true;
        }
        navigationFocus = false;
        mouseX = int(f.x * 800);
        mouseY = int(f.y * 600);
        if (e.type == SDL_FINGERDOWN) {
            if (touchLogCount++ < 8)
                fprintf(stderr, "Front touch: canvas=(%d,%d) screen=%d\n", mouseX, mouseY, int(uiScreen));
            pointerDown(mouseX, mouseY);
        } else
            dragSlider(mouseX);
        return true;
    }
    if (e.type == SDL_MOUSEMOTION) {
        if (e.motion.which != SDL_TOUCH_MOUSEID && (!e.motion.windowID || e.motion.windowID == windowId)) {
            if (mouseX != e.motion.x || mouseY != e.motion.y)
                navigationFocus = false;
            mouseX = e.motion.x;
            mouseY = e.motion.y;
            if (pointerHeld)
                dragSlider(mouseX);
        }
        return true;
    }
    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
        if (e.button.which == SDL_TOUCH_MOUSEID || (e.button.windowID && e.button.windowID != windowId))
            return true;
        if (e.type == SDL_MOUSEBUTTONUP) {
            if (e.button.button == SDL_BUTTON_LEFT)
                pointerUp(e.button.x, e.button.y);
            return true;
        }
        if (e.button.x < 0 || e.button.x >= 800 || e.button.y < 0 || e.button.y >= 600)
            return true;
        navigationFocus = false;
        mouseX = e.button.x;
        mouseY = e.button.y;
        if (e.button.button == SDL_BUTTON_RIGHT)
            action(uiScreen == UiNone ? 6 : 2);
        else if (e.button.button == SDL_BUTTON_MIDDLE)
            action(2);
        else if (e.button.button == SDL_BUTTON_LEFT)
            pointerDown(mouseX, mouseY);
        return true;
    }
    if (e.type == SDL_MOUSEWHEEL) {
        if (e.wheel.y == 0)
            return true;
        if (uiScreen == UiHistory)
            dispatch(22, e.wheel.y > 0 ? -1 : 1);
        else if (uiScreen == UiNone && inGame && e.wheel.y > 0)
            dispatch(20);
        else {
            SDL_Point point{mouseX, mouseY};
            for (const auto &b : uiButtons)
                if (b.enabled && b.action == 30 && SDL_PointInRect(&point, &b.rect)) {
                    changeSlider(b.value, e.wheel.y > 0 ? 1 : -1);
                    break;
                }
        }
        return true;
    }
    return false;
}
void inputName(const std::string &value, bool backspace) {
    if (nameField < 0)
        return;
    auto &s = nameField ? given : surname;
    if (backspace) {
        if (!s.empty()) {
            size_t n = s.size() - 1;
            while (n > 0 && (uint8_t(s[n]) & 192) == 128)
                n--;
            s.erase(n);
        }
    } else if (s.size() + value.size() <= 48)
        s += value;
    textCache.clear();
}
void originalAction(int a) {
    if (a == 3 || a == 4 || a == 7 || a == 8)
        navigationFocus = true;
    if (splash) {
        if (a == 1 || a == 2 || a == 6)
            finishSplash();
        return;
    }
    if (!fatal.empty()) {
        if (a == 1 || a == 2 || a == 6)
            running = false;
        return;
    }
    if (movie.active()) {
        if (a == 1 || a == 2 || a == 6) {
            fprintf(stderr, "Opening skipped by input: action=%d, time=%.3f s\n", a, movie.position());
            movie.stop();
            returnTitle();
        }
        return;
    }
    if (ending) {
        if (endingCanSkip && (a == 1 || a == 2)) {
            ending = false;
            endingTextures.clear();
            returnTitle();
        }
        return;
    }
    if (uiScreen != UiNone || menu) {
        if (a == 2 || a == 6) {
            if (uiScreen == UiNone)
                return;
            goBack();
            return;
        }
        if (uiScreen == UiHistory && (a == 3 || a == 4)) {
            dispatch(22, a == 3 ? -1 : 1);
            return;
        }
        if ((a == 7 || a == 8) && !uiButtons.empty() &&
            uiButtons[std::clamp(uiFocus, 0, int(uiButtons.size()) - 1)].action == 30) {
            changeSlider(uiButtons[uiFocus].value, a == 7 ? -1 : 1);
            return;
        }
        if (a == 3 || a == 4 || a == 7 || a == 8) {
            if (nameField >= 0) {
                nameField = -1;
                SDL_StopTextInput();
            }
            int n = int(uiButtons.size());
            if (n) {
                int direction = a == 3 || a == 7 ? -1 : 1;
                for (int i = 0; i < n; i++) {
                    uiFocus = (uiFocus + direction + n) % n;
                    if (uiButtons[uiFocus].enabled)
                        break;
                }
            }
            mouseX = mouseY = -1;
        }
        if (a == 1 && !uiButtons.empty()) {
            auto b = uiButtons[std::clamp(uiFocus, 0, int(uiButtons.size()) - 1)];
            if (b.enabled)
                dispatch(b.action, b.value);
        }
        return;
    }
    if (a == 6) {
        dispatch(2, 0);
        return;
    }
    if (a == 5) {
        dispatch(20);
        return;
    }
    if (a == 9) {
        skipMode = !skipMode && (messageRead || preferences.value("skipUnread", false));
        autoMode = false;
        return;
    }
    if (a == 10) {
        dispatch(21);
        return;
    }
    if (a == 2) {
        hidden = !hidden;
        return;
    }
    if (hidden) {
        if (a == 1)
            hidden = false;
        return;
    }
    if (!visual.at("choice").empty()) {
        int n = int(visual.at("choice").size());
        if (a == 3 || a == 7)
            choice = (choice + n - 1) % n;
        if (a == 4 || a == 8)
            choice = (choice + 1) % n;
        if (a == 1)
            dispatch(80, choice);
        return;
    }
    if (a == 1) {
        if (deadline || effectId) {
            deadline = 0;
            effectId = 0;
            advance();
            return;
        }
        if (autoMode || skipMode) {
            autoMode = skipMode = false;
            return;
        }
        if (revealed < message.length(page)) {
            revealed = message.length(page);
            markMessageRead();
            return;
        }
        if (preferences.value("interruptVoice", true))
            stopVoice();
        advance();
    }
}
bool beginEffect(int id, int step = 0) {
    if (!preferences.value("effects", true) || skipMode || heldSkip) {
        effectId = 0;
        return false;
    }
    int length = effectFrames(id, step);
    if (!length)
        return false;
    const char *mask = wipeName(id);
    if (mask && !asset("sys.a").contains(mask)) {
        fprintf(stderr, "Unused mask absent: %s\n", mask);
        return false;
    }
    if (!canvasReady)
        return false;
    // One buffer serves as texture source and snapshot pixels; wipes rewrite only its alpha bytes.
    uint32_t started = SDL_GetTicks();
    transitionFrame = std::make_shared<Pixels>(snapshots.last.render());
    effectCover.load(r, std::shared_ptr<const Pixels>(transitionFrame));
    if (SDL_GetTicks() - started >= 50)
        perfLog("transition snapshot", SDL_GetTicks() - started);
    transitionMask = mask ? background(asset("sys.a"), mask) : Pixels();
    if (mask && (transitionMask.w != 800 || transitionMask.h != 600))
        throw std::runtime_error("Invalid wipe dimensions");
    effectId = id;
    effectStep = transitionStep(id, step);
    effectStart = SDL_GetTicks();
    effectLength = length;
    effectFrame = -1;
    deadline = effectStart + uint32_t((length * 1000 + 59) / 60);
    return true;
}
void drawEffect() {
    if (!effectId)
        return;
    int frame = std::min(effectLength - 1, int((SDL_GetTicks() - effectStart) * 60 / 1000));
    if (effectId >= 4959) {
        auto sample = effectSample(effectId, frame);
        rectangle(0, 0, 800, 600, {0, 0, 0, 255});
        int w = int(800 * sample.zoom), h = int(600 * sample.zoom),
            x = int(399 * (1 - sample.zoom)) + sample.x, y = int(299 * (1 - sample.zoom)) + sample.y;
        effectCover.draw(r, x, y, w, h);
        if (sample.flash > 0)
            rectangle(std::max(0, x), std::max(0, y), std::max(0, std::min(800, x + w) - std::max(0, x)),
                      std::max(0, std::min(600, y + h) - std::max(0, y)),
                      {255, 255, 255, uint8_t(sample.flash * 255)});
    } else if (wipeName(effectId)) {
        if (frame != effectFrame) {
            // RGB always comes from the transition snapshot; every frame replaces all alpha bytes.
            auto &p = *transitionFrame;
            for (size_t i = 0; i < p.rgba.size(); i += 4)
                p.rgba[i + 3] = wipeAlpha(transitionMask.rgba[i], frame, effectStep);
            effectCover.update(std::shared_ptr<const Pixels>(transitionFrame));
            effectFrame = frame;
        }
        effectCover.draw(r, 0, 0);
    } else {
        SDL_SetTextureAlphaMod(effectCover.ptr, uint8_t(std::max(0, 127 - frame * effectStep) * 255 / 128));
        effectCover.draw(r, 0, 0);
        SDL_SetTextureAlphaMod(effectCover.ptr, 255);
    }
}
void drawPresentation() {
    snapshots.begin(canvas.ptr);
    SDL_SetRenderTarget(r, canvas.ptr);
    SDL_SetRenderDrawColor(r, 8, 12, 20, 255);
    SDL_RenderClear(r);
    uiButtons.clear();
    if (!fatal.empty())
        fallback(r,
                 "TENSHI VITA\n\n" + fatal +
                     "\n\nDETAILS: UX0:DATA/TENSHI/RUNTIME.LOG\nPRESS CROSS OR START TO EXIT",
                 32, 32);
    else if (splash)
        drawSplash((SDL_GetTicks() - splashAt) / 1000.f);
    else if (movie.active()) {
        if (!movie.draw(r)) {
            fprintf(stderr, "Opening completed: time=%.3f s\n", movie.position());
            movie.stop();
            returnTitle();
        }
    } else if (ending)
        drawEnding();
    else {
        if (menu) {
            title.draw(r, 0, 0);
            auto &overlay = uiTexture("title.px", 27);
            overlay.draw(r, 400 + overlay.originX, 200 + overlay.originY);
            if (uiScreen == UiNone) {
                int normal[] = {8, 10, 12, 14, 16}, actions[] = {1, 3, 2, 6, 7};
                for (int i = 0; i < 5; i++)
                    button("title.px", normal[i], normal[i] - 1, 310, 377 + 43 * i, actions[i],
                           i == 1   ? 1
                           : i == 2 ? 2
                                    : 0,
                           i != 1 || haveSaveSlots);
            }
        } else {
            drawSceneBase(visual);
            drawWeather();
            if (!hidden) {
                dialogueUi(visual, message, page, revealed, uiScreen == UiNone);
                if (uiScreen == UiNone && messageWaiting && !autoMode && !skipMode && !deadline &&
                    visual.at("choice").empty() && revealed >= message.length(page) &&
                    !message.pages.at(page).empty()) {
                    auto &last = message.pages.at(page).back();
                    uiImage("cursor.px",
                            std::min(int(SDL_GetTicks() * 60 / 1000 % 152), frameCount("cursor.px") - 1),
                            (message.full ? 74 : 154) +
                                int(std::min(last.x + last.advance + 4, message.full ? 624.f : 464.f)),
                            (message.full ? 20 : 504) + int(last.y) - last.size - 2);
                }
                drawClock();
                auto &choices = visual.at("choice");
                for (int i = 0; i < int(choices.size()); i++) {
                    button("common.px", i == choice ? 32 : 31, 32, 0, 240 + i * 35, 80, i, true, true, 32);
                    label(formatted(choices[i]), 0, 246 + i * 35, 800, 24, true);
                }
            }
            drawEffect();
        }
        if (uiScreen != UiNone)
            uiButtons.clear();
        switch (uiScreen) {
        case UiSettings:
            settingsUi();
            break;
        case UiSave:
            saveUi();
            break;
        case UiHistory:
            historyUi();
            break;
        case UiName:
            nameUi();
            break;
        case UiConfirm:
            confirmUi();
            break;
        default:
            if (uiScreen >= UiGallery && uiScreen <= UiBonus)
                galleryUi();
            break;
        }
        if (!notice.empty()) {
            rectangle(20, 12, 760, 30, {0, 0, 0, 225});
            label(notice, 28, 15, 742, 17);
        }
    }
    snapshots.finish();
    canvasReady = true;
    SDL_SetRenderTarget(r, nullptr);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    canvas.draw(r, 0, 0, 800, 600);
    SDL_RenderPresent(r);
    std::string stage = presentationStage(false);
    if (loggedStages.insert(stage).second)
        fprintf(stderr, "Presentation rendered: %s (800x600 original layout)\n", stage.c_str());
}
std::string presentationStage(bool menus = true) {
    return !fatal.empty()                                ? "error"
           : splash                                      ? "logo"
           : movie.active()                              ? "opening"
           : ending                                      ? "ending"
           : menu                                        ? "title"
           : menus && uiScreen != UiNone                 ? "menu"
           : visual.value("text", std::string()).empty() ? "scene"
                                                         : "dialogue";
}
// Rate-limited timing lines in runtime.log (stderr is unbuffered, so every line is a card write).
int perfLines = 0;
void perfLog(const char *what, uint32_t ms) {
    if (perfLines >= 80)
        return;
    perfLines++;
    fprintf(stderr, "Perf: %s %u ms (%s)\n", what, unsigned(ms), presentationStage().c_str());
}
void tickPresentation() {
    uint32_t now = SDL_GetTicks();
    double delta = tickAt ? std::min(0.1, (now - tickAt) / 1000.) : 0;
    tickAt = now;
    if (uiDirty && now - lastPersist > 5000) {
        persistUi();
        lastPersist = now;
    }
    if (galleryPlaying && audio && !Mix_PlayingMusic()) {
        if (playlist.empty())
            galleryPlaying = false;
        else {
            auto it = std::find(playlist.begin(), playlist.end(), musicId);
            size_t index = it == playlist.end() ? 0 : size_t(it - playlist.begin()) + 1;
            if (galleryShuffle)
                index = size_t(snowfall.rand(int(playlist.size())));
            if (index >= playlist.size() && !galleryRepeat)
                galleryPlaying = false;
            else
                playGalleryTrack(playlist[index % playlist.size()]);
        }
    }
    for (int i = 0; i < 8; i++) {
        auto &f = soundFades[i];
        if (!f.duration)
            continue;
        Mix_Volume(i + 1, fadeVolume(f, now));
        if (now - f.start >= f.duration) {
            if (!f.target)
                Mix_HaltChannel(i + 1);
            f.duration = 0;
        }
    }
    if (audio && musicFade.duration) {
        Mix_VolumeMusic(fadeVolume(musicFade, now));
        if (now - musicFade.start >= musicFade.duration) {
            Mix_HaltMusic();
            musicFade.duration = 0;
        }
    }
    if (audio && overlapFade.duration) {
        Mix_Volume(9, fadeVolume(overlapFade, now));
        if (now - overlapFade.start >= overlapFade.duration) {
            clearMusicOverlap();
        }
    }
    if (splash) {
        if (now - splashAt >= 9002)
            finishSplash();
        return;
    }
    if (ending && now - endingAt >= 273500) {
        ending = false;
        endingTextures.clear();
        returnTitle();
    }
    if (!fatal.empty() || !inGame || menu || uiScreen != UiNone || ending || movie.active())
        return;
    if (preferences.value("effects", true) && preferences.value("particles", true))
        snowfall.tick(delta);
    if (deadline) {
        if (now >= deadline || skipMode || heldSkip) {
            deadline = 0;
            effectId = 0;
            advance();
        }
        return;
    }
    if (hidden || !visual.at("choice").empty())
        return;
    bool skipping = skipMode || heldSkip;
    int speed = textMode == 3 ? 0 : preferences.at("textSpeed").get<int>();
    revealed = textMode == 0 || preferences.value("instant", false) || skipping
                   ? message.length(page)
                   : std::min(message.length(page), revealed + float(delta * 60 * (2 * speed + 1)));
    if (revealed < message.length(page)) {
        messageTimer = 0;
        return;
    }
    markMessageRead();
    if (skipping) {
        skipTimer += delta;
        if (skipTimer >= .06) {
            skipTimer = 0;
            advance();
        }
        return;
    }
    if (!messageWaiting && page + 1 == int(message.pages.size())) {
        advance();
        return;
    }
    if (!autoMode)
        return;
    if (voiceBusy()) {
        messageTimer = 0;
        return;
    }
    messageTimer += delta;
    int bytes = messageEvent.is_null() ? 0 : gb18030ByteCount(messageEvent.at("text"));
    int speedAuto = 10 - preferences.at("autoSpeed").get<int>();
    double frames = visual.value("voice", std::string()).empty()
                        ? 5 + speedAuto * 6 + speedAuto * bytes * 5 / (message.full ? 10 : 12)
                        : 5;
    if (messageTimer >= frames / 60.)
        advance();
}
bool splash = false, introSeen = false, endingCanSkip = false;
uint32_t splashAt = 0;
int logoDirections = 0, logoVariant = 0;
LogoRenderer logo;
Texture nameBackground, confirmShade;
std::string confirmShadeKey;
float glyphOpacity = 1;
void startOpening() {
    effectId = 0;
    deadline = 0;
    inGame = false;
    menu = false;
    stopVoice();
    stopSounds();
    stopMusic();
    movie.start(r, openingPath());
}
void finishSplash() {
    splash = false;
    if (introSeen)
        startOpening();
    else
        newGame();
}
void drawSplash(float time) {
    if (!logo.ready())
        logo.load(r, pxFrame(atlas("leaflogo.px"), 0), pxFrame(atlas("leaflogo.px"), 1));
    logo.draw(r, snapshots, time, logoDirections, logoVariant);
}
void verifyUi() {
    auto require = [](bool ok, const char *label) {
        if (!ok)
            throw std::runtime_error(label);
    };
    int screens = 0;
    std::set<uint64_t> hashes;
    auto check = [&]() {
        draw();
        require(!uiButtons.empty(), "Screen has no buttons");
        for (const auto &b : uiButtons)
            require(b.rect.x >= 0 && b.rect.y >= 0 && b.rect.x + b.rect.w <= 800 &&
                        b.rect.y + b.rect.h <= 600,
                    "UI button outside native canvas");
        Pixels out = snapshots.last.render();
        uint64_t hash = 14695981039346656037ull;
        for (auto byte : out.rgba)
            hash = (hash ^ byte) * 1099511628211ull;
        hashes.insert(hash);
        screens++;
    };
    introSeen = true;
    menu = true;
    showUi(UiNone);
    check();
    require(uiButtons.size() == 5, "Title controls");
    dispatch(1);
    check();
    dispatch(60, 0);
    std::string before = surname;
    inputName("A", false);
    inputName("", true);
    require(surname == before, "UTF-8 name edit");
    dispatch(62);
    check();
    dispatch(64);
    closeUi();
    for (int i = 0; i < 4; i++) {
        dispatch(2, i);
        check();
    }
    int volumeBefore = preferences.at("music");
    changeSlider(3, -1);
    require(preferences.at("music") == volumeBefore - 1, "Volume slider");
    changeSlider(3, 1);
    closeUi();
    resetStory();
    visual["background"] = 1;
    visual["speaker"] = 0;
    visual["text"] = "\xe5\xAD\x98\xE6\xA1\xA3\xe6\xb5\x8b\xe8\xaf\x95";
    paginate();
    revealed = message.length(page);
    historyScenes.push_back(visual);
    messageEvent = event(Dialogue, 1, "ui-test");
    messageEvent["text"] = visual.at("text");
    captureDialogueAnchor();
    draw();
    for (int n : {1, 8, 9, 16, 17, 24, 25, 32, 33, 40}) {
        slot = n;
        saveGame();
    }
    dispatch(3, 1);
    for (int i = 0; i < 5; i++) {
        dispatch(5, i);
        check();
    }
    slot = 40;
    loadGame();
    require(visual.at("background") == 1 && revealed == 0, "Save roundtrip");
    dispatch(20);
    check();
    dispatch(24);
    for (int full = 0; full < 2; full++) {
        visual["full"] = bool(full);
        paginate();
        revealed = message.length(page);
        check();
    }
    visual["full"] = false;
    visual["choice"] = {"A", "B", "C"};
    check();
    dispatch(2, 0);
    draw();
    for (auto &b : uiButtons)
        require(b.action != 80, "Modal UI leaked a story choice");
    dispatch(31, 5);
    closeUi();
    require(autoMode, "Auto mode lost when closing settings");
    autoMode = false;
    visual["choice"] = Json::array();
    returnTitle();
    dispatch(6);
    check();
    dispatch(50, 0);
    for (int i = 0; i < 5; i++) {
        dispatch(52, i);
        check();
    }
    showUi(UiMemoirs);
    check();
    showUi(UiBonus);
    check();
    for (int i = 0; i < 6; i++) {
        dispatch(55, i);
        finishVoiceDecode();
        require(!audio || Mix_Playing(0), "Bonus voice did not play");
    }
    stopVoice();
    showUi(UiMusic);
    check();
    dispatch(56, 24);
    dispatch(56, 24);
    require(galleryPlaying, "Music gallery playback");
    dispatch(57);
    require(!galleryPlaying, "Music gallery stop");
    for (const auto &name : cgNames) {
        auto file = "eg" + name + ".px";
        if (asset("egbg.a").contains(file)) {
            galleryImage = file;
            showUi(UiCgImage);
            check();
            break;
        }
    }
    require(hashes.size() >= 20, "UI screens rendered identically");
    fprintf(stderr,
            "UI smoke passed: %d screens, %zu distinct canvas hashes, 40-slot paging, name editing, history "
            "and audio controls\n",
            screens, hashes.size());
    returnTitle();
}
void verifyEffects() {
    resetStory();
    visual["background"] = 1;
    visual["text"] = "Effect timing";
    paginate();
    revealed = message.length(page);
    draw();
    int total = 0;
    for (int id : {2, 3, 12, 30, 31, 32, 33, 34, 35, 36, 37, 41, 50, 4959, 5251, 6036, 6200}) {
        if (wipeName(id) && !asset("sys.a").contains(wipeName(id)))
            continue;
        if (!beginEffect(id))
            throw std::runtime_error("Effect did not start");
        for (int frame = 0; frame < effectLength; frame++) {
            effectStart = SDL_GetTicks() - uint32_t((frame * 1000 + 59) / 60);
            draw();
            total++;
        }
        effectId = 0;
        deadline = 0;
    }
    for (int type = 0; type < 7; type++) {
        snowfall.start(type * 8 + 3);
        for (int frame = 0; frame < 1000; frame++) {
            snowfall.tick(1. / 60);
            if (frame % 100 == 0)
                draw();
        }
        for (const auto &p : snowfall.particles)
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                throw std::runtime_error("Nonfinite weather particle");
    }
    snowfall.start(0);
    for (int value : {42, 43, 0}) {
        visual["tone"] = value;
        draw();
    }
    for (float t : {0.f, 2.2f, 3.1f, 4.4f, 4.7f, 5.7f, 9.f}) {
        SDL_SetRenderTarget(r, canvas.ptr);
        drawSplash(t);
        SDL_SetRenderTarget(r, nullptr);
    }
    for (int route = 1; route <= 5; route++) {
        endingRoute = route;
        prepareEnding();
        ending = true;
        for (int seconds : {0, 4, 8, 10, 14, 16, 23, 27, 90, 150, 260, 270}) {
            endingAt = SDL_GetTicks() - uint32_t(seconds * 1000);
            draw();
        }
        ending = false;
    }
    fprintf(stderr,
            "Effects smoke passed: %d transition frames, 7 weather modes, 3 tones, logo phases and 5 ending "
            "timelines\n",
            total);
    returnTitle();
}

#include "input_smoke.inc"

#include "startup_smoke.inc"
