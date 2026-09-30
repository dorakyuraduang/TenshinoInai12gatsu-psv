// Included only in a generated host audit build, never in the shipped game.
void auditParity() {
    Json results = Json::array();
    auto record = [&](const char *id, const Json &actual, const Json &expected) {
        results.push_back(
            {{"id", id}, {"actual", actual}, {"godotExpected", expected}, {"different", actual != expected}});
        fprintf(stderr, "Parity %s: %s\n", id, actual == expected ? "same" : "DIFFERENT");
    };
    auto require = [](bool ok, const char *why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const auto reference = readJson(stateRoot + "/startup.json");
    const int baseSize = int(reference.at("events").size());
    std::string voiceName;
    for (const auto &e : reference.at("events"))
        if (e.at("kind") == Dialogue && !e.at("voice").get<std::string>().empty()) {
            voiceName = e.at("voice");
            break;
        }
    require(!voiceName.empty(), "Audit needs a voiced reference event");
    auto line = [](int id, const std::string &voice = "") {
        auto e = event(Dialogue, id, "audit.p");
        e["text"] = "Audit dialogue for parity checks.";
        e["voice"] = voice;
        return e;
    };
    auto inject = [&](const Json &events) {
        auto data = vm->save();
        data["prologue"] = baseSize;
        data["pending"] = events;
        vm->load(data);
    };
    auto scene = [&]() {
        resetStory();
        splash = false;
        introSeen = true;
        preferences["effects"] = false;
        preferences["instant"] = false;
        preferences["readSkip"] = false;
        preferences["skipUnread"] = false;
        textMode = 1;
        readMessages.clear();
        messageRead = false;
        heldSkip = false;
    };
    record("default-text-speed", preferences.at("textSpeed"), 5);
    record("default-auto-speed", preferences.at("autoSpeed"), 5);
    record("default-master-volume-steps", preferences.at("master"), 20);

    for (int row : {1, 2}) {
        showUi(UiGallery);
        draw();
        auto it = std::find_if(uiButtons.begin(), uiButtons.end(),
                               [=](const auto &b) { return b.action == 50 && b.value == row; });
        require(it != uiButtons.end(), "Missing gallery button");
        SDL_Rect rect = it->rect;
        pointer(rect.x + rect.w / 2, rect.y + rect.h / 2);
        auto name = uiScreen == UiMusic ? "music" : uiScreen == UiMemoirs ? "memoirs" : "other";
        record(row == 1 ? "memoirs-button-target" : "music-button-target", name,
               row == 1 ? "memoirs" : "music");
    }
    scene();
    autoMode = true;
    dispatch(2, 0);
    closeUi();
    record("auto-survives-settings", autoMode, true);
    skipMode = true;
    dispatch(2, 0);
    closeUi();
    record("skip-survives-settings", skipMode, true);

    dispatch(2, 3);
    draw();
    auto sliderIt = std::find_if(uiButtons.begin(), uiButtons.end(),
                                 [](const auto &b) { return b.action == 30 && b.value == 2; });
    require(sliderIt != uiButtons.end(), "Missing master slider");
    SDL_Rect sliderRect = sliderIt->rect;
    SDL_Event down{};
    down.type = SDL_FINGERDOWN;
    down.tfinger.touchId = 1;
    down.tfinger.fingerId = 7;
    down.tfinger.x = (sliderRect.x + 2) / 800.f;
    down.tfinger.y = (sliderRect.y + sliderRect.h / 2) / 600.f;
    pointerEvent(down);
    require(preferences.at("master") == 1, "Slider press setup failed");
    down.type = SDL_FINGERMOTION;
    down.tfinger.x = (sliderRect.x + sliderRect.w - 2) / 800.f;
    pointerEvent(down);
    record("slider-drag-to-end", preferences.at("master"), 20);
    down.type = SDL_FINGERUP;
    pointerEvent(down);
    closeUi();

    scene();
    inject(Json::array({line(1), line(2)}));
    advance();
    require(messageKey == "audit.p:1" && revealed == 0, "Dialogue setup failed");
    originalAction(1);
    originalAction(1);
    require(messageKey == "audit.p:2", "Two-click setup did not advance");
    record("read-mark-two-clicks-one-frame", readMessages.count("audit.p:1") != 0, true);
    dispatch(20);
    historyAt = int(historyScenes.size()) - 1;
    dispatch(22, 1);
    record("history-forward-past-last-closes", uiScreen == UiNone, true);
    closeUi();

    scene();
    inject(Json::array({line(3)}));
    advance();
    readMessages.insert(messageKey);
    messageRead = true;
    const auto savedKey = messageKey;
    slot = 1;
    saveGame();
    messageKey = "different.p:8";
    messageRead = false;
    loadGame();
    record("load-restores-message-key", messageKey, savedKey);
    record("load-recomputes-read-status", messageRead, true);
    record("load-starts-page-reveal-at-zero", revealed, 0.0);

    scene();
    preferences["interruptVoice"] = false;
    inject(Json::array({line(4, voiceName), line(5)}));
    advance();
    finishVoiceDecode(); // voices are decoded on a worker thread
    require(Mix_Playing(0), "Voice did not start");
    advance();
    finishVoiceDecode();
    record("continue-voice-on-unvoiced-line", Mix_Playing(0) != 0, true);
    scene();
    preferences["readSkip"] = true;
    readMessages.insert("audit.p:6");
    inject(Json::array({line(6, voiceName)}));
    advance();
    require(skipMode, "Read-skip setup failed");
    finishVoiceDecode();
    record("skip-mode-does-not-start-voice", Mix_Playing(0) == 0, true);

    scene();
    playMusic(8);
    require(Mix_PlayingMusic(), "BGM did not start");
    auto stop = event(Music, 0);
    stop["slot"] = 100;
    inject(Json::array({stop, line(7)}));
    advance();
    record("music-stop-keeps-playing-during-fade", Mix_PlayingMusic() != 0, true);

    scene();
    visual["speaker"] = 0;
    inject(Json::array({event(MessageWindow, 0), line(8)}));
    advance();
    record("hide-window-clears-speaker", visual.at("speaker"), 255);

    scene();
    inject(Json::array({line(9), line(10)}));
    advance();
    heldSkip = true;
    tickAt = SDL_GetTicks() - 70;
    tickPresentation();
    record("ctrl-can-skip-unread", messageKey != "audit.p:9", true);
    heldSkip = false;
    scene();
    playVoice(voiceName);
    finishVoiceDecode();
    require(Mix_Playing(0), "Choice voice setup failed");
    skipMode = true;
    auto choiceEvent = event(Choice);
    choiceEvent["options"] = Json::array({"A", "B"});
    inject(Json::array({choiceEvent}));
    auto choiceState = vm->save();
    choiceState["waiting"] = true;
    vm->load(choiceState);
    advance();
    require(!visual.at("choice").empty(), "Choice setup failed");
    finishVoiceDecode();
    record("enter-choice-stops-voice", Mix_Playing(0) == 0, true);
    record("enter-choice-clears-skip", skipMode, false);

    // Save the last dialogue scene even if a following pause/choice has changed the VM.
    scene();
    auto pause = event(Pause, 20);
    inject(Json::array({line(11), pause, line(12)}));
    advance();
    auto dialogueState = vm->save();
    advance();
    require(deadline != 0 && vm->save() != dialogueState, "Pause setup failed");
    slot = 2;
    saveGame();
    loadGame();
    record("save-during-pause-restores-dialogue",
           vm->save() == dialogueState && messageKey == "audit.p:11" && deadline == 0, true);
    auto validSave = readJson(savePath());
    showUi(UiSettings);
    auto beforeBadVm = vm->save(), beforeBadVisual = visual;
    auto badSave = validSave;
    badSave["runtime"]["scriptHash"] = "invalid";
    writeState("save2.json", badSave);
    loadGame();
    record("wrong-script-save-keeps-current-scene",
           vm->save() == beforeBadVm && visual == beforeBadVisual && uiScreen == UiSettings && fatal.empty(),
           true);
    badSave = validSave;
    badSave["pendingAdvance"] = "invalid";
    writeState("save2.json", badSave);
    loadGame();
    record("malformed-save-keeps-current-scene",
           vm->save() == beforeBadVm && visual == beforeBadVisual && uiScreen == UiSettings && fatal.empty(),
           true);
    badSave = validSave;
    badSave["visual"]["characters"] = "invalid";
    writeState("save2.json", badSave);
    showUi(UiSave);
    loadingSlots = true;
    draw();
    record("malformed-slot-preview-is-unavailable", slotInfo(2).is_null() && fatal.empty(), true);
    writeState("save2.json", validSave);
    showUi(UiSave);
    loadingSlots = true;
    dispatch(4, 2);
    record("load-slot-opens-without-confirmation", uiScreen == UiNone && messageKey == "audit.p:11", true);
    replayMode = true;
    auto replayState = vm->save();
    replayState["replay"] = true;
    vm->load(replayState);
    captureDialogueAnchor();
    slot = 3;
    saveGame();
    returnTitle();
    loadGame();
    record("replay-save-and-load-allowed", replayMode && inGame && messageKey == "audit.p:11", true);
    replayState = vm->save();
    replayState["pending"] = Json::array();
    replayState["finished"] = true;
    vm->load(replayState);
    advance();
    record("replay-completion-returns-title", menu && !inGame && uiScreen == UiNone, true);
    showUi(UiCg);
    goBack();
    record("gallery-back-closes-to-title", menu && uiScreen == UiNone, true);
    scene();
    showUi(UiSave);
    ask("overwrite", 72, 2);
    dispatch(64);
    record("system-confirm-cancel-closes-overlay", uiScreen == UiNone, true);

    // Auto timing uses the active raw event, including GB18030 names/control bytes.
    scene();
    preferences["autoSpeed"] = 5;
    auto first = line(20), second = line(21), third = line(22);
    first["text"] = std::string(300, 'A');
    first["flags"] = 1;
    second["text"] = "Z";
    inject(Json::array({first, second, third}));
    advance();
    revealed = message.length(page);
    while (page + 1 < int(message.pages.size())) {
        advance();
        revealed = message.length(page);
    }
    advance();
    revealed = message.length(page);
    autoMode = true;
    require(messageEvent.at("text") == "Z" && visual.at("text").get<std::string>().size() == 301,
            "Append timing setup failed");
    messageTimer = 36.0 / 60;
    tickAt = SDL_GetTicks() - 1;
    tickPresentation();
    record("auto-append-waits-active-event-before-threshold", messageKey, "audit.p:21");
    tickAt = SDL_GetTicks() - 20;
    tickPresentation();
    record("auto-append-advances-at-raw-event-threshold", messageKey, "audit.p:22");

    scene();
    playMusic(8);
    SDL_Delay(25);
    playMusic(9);
    record("music-change-overlaps-old-track",
           Mix_PlayingMusic() != 0 && Mix_Playing(9) != 0 && outgoingMusic != nullptr, true);
    record("music-overlap-bounded-memory", outgoingMusic && outgoingMusic->alen <= 44100 * 4 * .31, true);
    overlapFade.start = SDL_GetTicks() - 150;
    tickPresentation();
    record("music-fade-uses-decibel-midpoint", Mix_Volume(9, -1) <= 2, true);
    overlapFade.start = SDL_GetTicks() - 301;
    tickPresentation();
    record("music-overlap-is-released",
           outgoingMusic == nullptr && Mix_Playing(9) == 0 && Mix_PlayingMusic() != 0, true);
    playSound(1, true);
    require(sounds[0] != nullptr && Mix_Playing(1), "Sound fade setup failed");
    auto seStop = event(StopSound, 1);
    seStop["slot"] = 1000;
    seStop["flags"] = 0;
    inject(Json::array({seStop, line(23)}));
    advance();
    soundFades[0].start = SDL_GetTicks() - 500;
    tickPresentation();
    record("sound-fade-uses-decibel-midpoint", Mix_Volume(1, -1) <= 2 && Mix_Playing(1) != 0, true);

    scene();
    clearedEndings.erase(1);
    inject(Json::array({event(Ending, 1)}));
    advance();
    auto persistedEndings = readJson(stateRoot + "/presentation.json").at("endings");
    record("ending-unlock-written-on-entry",
           std::find(persistedEndings.begin(), persistedEndings.end(), 1) != persistedEndings.end(), true);
    record("ending-music-plays-once", musicLoops, false);
    returnTitle();
    startOpening();
    SDL_Delay(20);
    draw();
    double movieAt = movie.position();
    setSuspended(true);
    SDL_Delay(35);
    draw();
    record("background-keeps-movie-paused", movie.active() && movie.isPaused() && movie.position() == movieAt,
           true);
    setSuspended(false);
    SDL_Delay(10);
    draw();
    record("foreground-resumes-same-movie",
           movie.active() && !movie.isPaused() && movie.position() > movieAt &&
               movie.position() - movieAt < .08,
           true);
    movie.stop();
    returnTitle();
    scene();
    skipMode = true;
    dispatch(2, 0);
    dispatch(31, 5);
    closeUi();
    record("settings-auto-choice-stops-skip", autoMode && !skipMode, true);
    int different = 0;
    for (const auto &result : results)
        different += result.at("different").get<bool>();
    writeState(
        "audit-results.json",
        {{"version", "PSV 0.17"}, {"checks", results}, {"total", results.size()}, {"different", different}});
    fprintf(stderr, "Parity audit finished: %d/%d checks differ from source-derived Godot expectations\n",
            different, int(results.size()));
    require(different == 0, "Parity regression: see audit-results.json");
}
