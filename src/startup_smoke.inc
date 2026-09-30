void verifyStartup() {
    const auto reference = readJson(stateRoot + "/startup.json");
    auto require = [](bool ok, const char *label) {
        if (!ok)
            throw std::runtime_error(label);
    };
    auto key = [](const Json &e) {
        return e.at("source").get<std::string>() + ":" + std::to_string(e.at("value").get<int>());
    };
    // The caller must provide an isolated state directory; these are test settings/saves.
    introSeen = false;
    readMessages.clear();
    preferences["effects"] = false;
    uiDirty = true;
    persistUi();
    splash = true;
    finishSplash();
    for (int guard = 0; messageKey.empty() && !movie.active() && guard < 100; guard++)
        advance();
    require(inGame && !movie.active() && messageKey == key(reference.at("firstDialogue")),
            "Fresh startup differs from Godot");
    int steps = 0;
    while (!movie.active() && steps++ < 1000) {
        require(messageKey.empty() || messageKey.rfind("com_0100.p:", 0) == 0,
                "First-launch intro leaked into normal story");
        revealed = message.length(page);
        advance();
    }
    require(movie.active() && !inGame && introSeen, "Intro did not enter opening");
    require(readJson(stateRoot + "/presentation.json").at("introSeen") == true,
            "Intro completion was not written before opening");
    movie.stop();
    returnTitle();
    introSeen = false;
    loadUi();
    require(introSeen, "Completion did not survive settings reload");
    splash = true;
    finishSplash();
    require(movie.active() && !inGame, "Return launch must play OP as Godot does");
    action(1);
    require(menu && !movie.active(), "Skipping OP did not enter title");
    newGame();
    for (int guard = 0; messageKey.empty() && guard < 100; guard++)
        advance();
    require(messageKey == key(reference.at("postIntroDialogue")), "New game skipped com_0110/com_0120");
    slot = 1;
    readMessages.insert(messageKey);
    uiDirty = true;
    auto runtimeBefore = vm->save(), visualBefore = visual;
    saveGame();
    require(haveSaveSlots && readJson(stateRoot + "/presentation.json").at("read").size() > 0,
            "Manual save did not flush read history");
    // 0.16 and earlier saved VM/visual only; exercise their actual load path too.
    auto oldSlot = readJson(savePath());
    oldSlot.erase("message");
    oldSlot.erase("messageStateVersion");
    writeState("save1.json", oldSlot);
    returnTitle();
    loadGame();
    require(messageKey == key(reference.at("postIntroDialogue")) && messageRead && revealed == 0,
            "Legacy slot did not recover reading metadata");
    require(inGame && vm->save() == runtimeBefore && visual == visualBefore,
            "Story save/load did not restore the same scene");
    // Old releases could persist post-intro read history while introSeen remained false.
    auto legacy = readJson(stateRoot + "/presentation.json");
    legacy["introSeen"] = false;
    legacy["read"] = Json::array({key(reference.at("postIntroDialogue"))});
    writeState("presentation.json", legacy);
    introSeen = false;
    readMessages.clear();
    loadUi();
    require(introSeen && readJson(stateRoot + "/presentation.json").at("introSeen") == true,
            "Old post-intro progress was not migrated");
    loadUi();
    require(introSeen, "Migrated progress was not persistent");
    legacy["read"] = Json::array({key(reference.at("firstDialogue"))});
    writeState("presentation.json", legacy);
    introSeen = false;
    readMessages.clear();
    loadUi();
    require(!introSeen, "Incomplete first-launch intro was incorrectly marked complete");
    fprintf(stderr,
            "Startup/save smoke passed: fresh intro -> OP -> title, persisted restart -> OP, "
            "normal story at %s, manual save/load, legacy migration and incomplete-intro preservation\n",
            key(reference.at("postIntroDialogue")).c_str());
    returnTitle();
    // Leave a 0.14-style record for a separate process to migrate on its next launch.
    legacy["read"] = Json::array({key(reference.at("postIntroDialogue"))});
    writeState("presentation.json", legacy);
}
void verifyRestart() {
    if (!introSeen || !haveSaveSlots || readJson(stateRoot + "/presentation.json").at("introSeen") != true)
        throw std::runtime_error("Fresh-process startup did not load/migrate progress");
    splash = true;
    finishSplash();
    if (!movie.active() || inGame)
        throw std::runtime_error("Fresh-process startup replayed the intro story");
    action(1);
    if (!menu || movie.active())
        throw std::runtime_error("Fresh-process OP skip did not enter title");
    slot = 1;
    auto saved = readJson(savePath());
    loadGame();
    if (!inGame || vm->save() != saved.at("runtime") || visual != saved.at("visual"))
        throw std::runtime_error("Fresh-process load did not restore saved story");
    fprintf(stderr,
            "Fresh-process restart passed: legacy progress migrated, OP -> title, manual save restored\n");
}
