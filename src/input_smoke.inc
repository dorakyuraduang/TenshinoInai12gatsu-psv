// Inject raw window/normalized touch events through SDL's real event queue.
// This exercises the renderer's logical-coordinate filter and production hit testing.
void verifyInput() {
    auto require = [](bool ok, const char *label) {
        if (!ok)
            throw std::runtime_error(label);
    };
    int events = 0;
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    auto send = [&](SDL_Event e) {
        require(SDL_PushEvent(&e) == 1, "Input event could not be queued");
        SDL_Event queued;
        bool delivered = false;
        while (SDL_PollEvent(&queued)) {
            if (queued.type == e.type)
                delivered = true;
            pointerEvent(queued, true);
        }
        require(delivered, "Input event was not delivered");
        events++;
    };
    auto finger = [&](Uint32 type, float x, float y, SDL_TouchID device = 1, SDL_FingerID id = 17) {
        SDL_Event e{};
        e.type = type;
        e.tfinger.windowID = SDL_GetWindowID(window);
        e.tfinger.touchId = device;
        e.tfinger.fingerId = id;
        e.tfinger.x = x;
        e.tfinger.y = y;
        e.tfinger.pressure = type == SDL_FINGERUP ? 0 : 1;
        send(e);
    };
    auto touch = [&](Uint32 type, int x, int y, SDL_TouchID device = 1, SDL_FingerID id = 17) {
        int wx, wy, w, h;
        SDL_RenderLogicalToWindow(r, float(x), float(y), &wx, &wy);
        SDL_GetWindowSize(window, &w, &h);
        finger(type, (wx + 0.25f) / w, (wy + 0.25f) / h, device, id);
    };
    auto mouse = [&](int x, int y, Uint32 device = 0, Uint8 button = SDL_BUTTON_LEFT) {
        SDL_Event e{};
        e.type = SDL_MOUSEBUTTONDOWN;
        e.button.windowID = SDL_GetWindowID(window);
        e.button.which = device;
        e.button.button = button;
        SDL_RenderLogicalToWindow(r, float(x), float(y), &e.button.x, &e.button.y);
        send(e);
        e.type = SDL_MOUSEBUTTONUP;
        SDL_RenderLogicalToWindow(r, float(x), float(y), &e.button.x, &e.button.y);
        send(e);
    };
    auto findButton = [&](int action, int value = 0) {
        draw();
        for (const auto &b : uiButtons)
            if (b.enabled && b.action == action && b.value == value)
                return b.rect;
        throw std::runtime_error("Input test button not found: " + std::to_string(action));
    };
    auto tap = [&](int x, int y) {
        touch(SDL_FINGERDOWN, x, y);
        touch(SDL_FINGERUP, x, y);
    };
    auto tapButton = [&](int action, int value = 0) {
        auto box = findButton(action, value);
        tap(box.x + box.w / 2, box.y + box.h / 2);
    };
    introSeen = true;
    returnTitle();
    draw();
    // Physical center of the Vita screen is the center of the original canvas.
    finger(SDL_FINGERDOWN, 0.5f, 0.5f);
    require(std::abs(mouseX - 400) <= 2 && std::abs(mouseY - 300) <= 2, "Vita center mapping");
    finger(SDL_FINGERUP, 0.5f, 0.5f);
    for (SDL_Point p : {SDL_Point{12, 12}, {788, 588}, {150, 250}, {650, 250}}) {
        touch(SDL_FINGERDOWN, p.x, p.y);
        require(std::abs(mouseX - p.x) <= 2 && std::abs(mouseY - p.y) <= 2, "Letterbox mapping");
        touch(SDL_FINGERUP, p.x, p.y);
    }
    // The production loop polls only after draw() restores the window target.
    draw();
    require(SDL_GetRenderTarget(r) == nullptr, "Event pump must use window target");
    finger(SDL_FINGERDOWN, 0.5f, 0.5f);
    require(std::abs(mouseX - 400) <= 2 && std::abs(mouseY - 300) <= 2, "Mapping after scene rendering");
    finger(SDL_FINGERUP, 0.5f, 0.5f);
    finger(SDL_FINGERDOWN, 0.01f, 0.5f);
    require(mouseX == -1 && mouseY == -1, "Black bar touch was not rejected");
    finger(SDL_FINGERUP, 0.01f, 0.5f);
    auto start = findButton(1);
    int sx = start.x + start.w / 2, sy = start.y + start.h / 2;
    touch(SDL_FINGERDOWN, sx, sy, 2);
    touch(SDL_FINGERUP, sx, sy, 2);
    require(uiScreen == UiNone, "Rear panel activated a title button");
    touch(SDL_FINGERDOWN, sx, sy, SDL_MOUSE_TOUCHID);
    touch(SDL_FINGERUP, sx, sy, SDL_MOUSE_TOUCHID);
    mouse(sx, sy, SDL_TOUCH_MOUSEID);
    require(uiScreen == UiNone, "Synthetic input activated a title button");
    touch(SDL_FINGERDOWN, sx, sy);
    require(uiScreen == UiNone, "Button activated before touch release");
    touch(SDL_FINGERUP, sx, sy);
    require(uiScreen == UiName, "Front touch did not open name screen");
    auto confirm = findButton(62);
    int cx = confirm.x + confirm.w / 2, cy = confirm.y + confirm.h / 2;
    touch(SDL_FINGERDOWN, cx, cy); // Repeated down while the same finger is held.
    touch(SDL_FINGERMOTION, cx, cy);
    touch(SDL_FINGERDOWN, cx, cy, 1, 18); // A second finger must not confirm.
    touch(SDL_FINGERUP, cx, cy, 1, 18);
    require(uiScreen == UiName && touchPressed, "Held/secondary finger activated twice");
    touch(SDL_FINGERUP, 12, 12); // Leaving the pressed button cancels activation.
    require(uiScreen == UiName, "Release outside button was not cancelled");
    tap(cx, cy);
    require(uiScreen == UiConfirm, "Name confirm button ignored touch");
    draw();
    mouse(cx, cy, SDL_TOUCH_MOUSEID);
    require(uiScreen == UiConfirm, "Synthetic mouse double-activated confirmation");
    tapButton(64);
    require(uiScreen == UiName, "Cancel confirmation ignored touch");
    returnTitle();
    tapButton(2, 2);
    require(uiScreen == UiSettings && settingsTab == 2, "Title settings ignored touch");
    tapButton(2, 3);
    auto slider = findButton(30, 3);
    tap(slider.x + 27, slider.y + 15);
    require(preferences.at("music") == 3, "Audio slider position incorrect");
    touch(SDL_FINGERDOWN, slider.x + 2, slider.y + 15);
    touch(SDL_FINGERMOTION, slider.x + slider.w - 2, slider.y + 15);
    require(preferences.at("music") == 20, "Queued slider drag did not reach maximum");
    touch(SDL_FINGERUP, slider.x + slider.w - 2, slider.y + 15);
    SDL_Event motion{};
    motion.type = SDL_MOUSEMOTION;
    motion.motion.windowID = SDL_GetWindowID(window);
    SDL_RenderLogicalToWindow(r, float(slider.x + 20), float(slider.y + 15), &motion.motion.x,
                              &motion.motion.y);
    send(motion);
    SDL_Event wheel{};
    wheel.type = SDL_MOUSEWHEEL;
    wheel.wheel.y = -1;
    send(wheel);
    require(preferences.at("music") == 19, "Slider wheel did not decrease value");
    tap(slider.x + 27, slider.y + 15);
    require(!navigationFocus, "Touch left a navigation focus indicator");
    draw();
    auto pointerSlider = snapshots.last.render().rgba;
    action(8);
    require(navigationFocus && preferences.at("music") == 4, "Directional slider focus failed");
    action(7); // Return to the same value so only the focus presentation differs.
    draw();
    require(snapshots.last.render().rgba != pointerSlider, "Directional slider has no visible focus");
    tap(slider.x + 27, slider.y + 15);
    draw();
    require(!navigationFocus && snapshots.last.render().rgba == pointerSlider,
            "Touch did not remove the navigation pointer");

    tapButton(2, 1);
    tapButton(3, 1);
    require(uiScreen == UiSave && loadingSlots, "Load screen ignored touch");
    tapButton(5, 4);
    require(savePage == 4, "Save page button ignored touch");
    require(!navigationFocus, "Page button kept a navigation pointer after touch");
    // Rendering a button without a hot frame must not depend on uiFocus in pointer mode.
    draw();
    auto pointerPage = snapshots.last.render().rgba;
    int clickedFocus = uiFocus;
    uiFocus = -1;
    draw();
    require(snapshots.last.render().rgba == pointerPage, "Pointer focus still changes button rendering");
    uiFocus = clickedFocus;
    action(3);
    draw();
    require(navigationFocus && snapshots.last.render().rgba != pointerPage,
            "Directional button navigation lost its visible cue");
    mouse(12, 12);
    require(!navigationFocus, "Mouse did not exit directional focus mode");

    returnTitle();
    start = findButton(1);
    mouse(start.x + start.w / 2, start.y + start.h / 2);
    require(uiScreen == UiName, "Desktop mouse no longer activates title buttons");
    resetStory();
    visual["background"] = 1;
    visual["text"] = "Touch input regression";
    visual["speaker"] = 0;
    visual["full"] = false;
    paginate();
    revealed = 0;
    messageWaiting = true;
    deadline = 0;
    effectId = 0;
    draw();
    tap(400, 400);
    require(revealed == message.length(page), "Story touch did not reveal the current page");
    autoMode = false;
    auto autoBox = findButton(21);
    int ax = autoBox.x + autoBox.w / 2, ay = autoBox.y + autoBox.h / 2;
    draw();
    auto unpressedAuto = snapshots.last.render().rgba;
    touch(SDL_FINGERDOWN, ax, ay);
    draw();
    require(snapshots.last.render().rgba != unpressedAuto, "Auto button has no pressed appearance");
    require(!autoMode, "Auto icon activated before touch release");
    touch(SDL_FINGERMOTION, ax, ay);
    touch(SDL_FINGERDOWN, ax, ay);
    mouse(ax, ay, SDL_TOUCH_MOUSEID);
    require(!autoMode, "Repeated down or synthetic mouse activated held button");
    touch(SDL_FINGERUP, ax, ay);
    require(autoMode, "Auto icon ignored touch release");
    tap(ax, ay);
    require(!autoMode, "Second independent touch did not toggle auto");
    historyScenes.push_back(visual);
    tapButton(20);
    require(uiScreen == UiHistory, "History icon ignored touch");
    closeUi();
    tapButton(2, 0);
    require(uiScreen == UiSettings, "Story settings icon ignored touch");
    fprintf(stderr,
            "Input smoke passed: %d queued events; front touch, letterbox, title, "
            "name/confirm, settings, slider, save paging, story icons, pointer/directional focus rendering, "
            "mouse and duplicate rejection\n",
            events);
    returnTitle();
}
