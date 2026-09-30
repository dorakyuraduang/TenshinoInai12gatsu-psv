#pragma once
// Frozen CPU renderer from the pre-H.264 release; kept as a pixel oracle.
inline tenshi::Pixels logoReference(const tenshi::Pixels &logoParts, const tenshi::Pixels &logoFinal,
                                    float time, int logoDirections, int logoVariant) {
    int gray = time < 4.4f     ? std::min(255, (int(time / .05f) + 1) * 3)
               : time < 5.002f ? 255
                               : std::max(0, 255 - (int((time - 5.002f) / .05f) + 1) * 4);
    // One persistent frame buffer (also the snapshot source) instead of allocating and copying
    // 1.9 MB several times per frame.
    tenshi::Pixels out(800, 600);
    for (size_t i = 0; i < 800 * 4; i += 4) {
        out.rgba[i] = out.rgba[i + 1] = out.rgba[i + 2] = uint8_t(gray);
        out.rgba[i + 3] = 255;
    }
    for (int row = 1; row < 600; row++)
        std::memcpy(&out.rgba[size_t(row) * 800 * 4], out.rgba.data(), 800 * 4);
    static const float durations[8][3] = {{2.2f, 2.2f, 2.2f},  {2.2f, 1.1f, 1.65f},   {2.2f, 1.65f, 1.1f},
                                          {1.1f, 2.2f, 1.65f}, {1.65f, 2.2f, 1.1f},   {1.1f, 1.65f, 2.2f},
                                          {1.65f, 1.1f, 2.2f}, {2.2f / 3, 2.2f, 2.2f}};
    float starts[3][2] = {{float(logoDirections & 1 ? -700 : 800), 200},
                          {50, float(logoDirections & 2 ? -240 : 800)},
                          {float(logoDirections & 4 ? 50 : 800), float(logoDirections & 4 ? 600 : -700)}};
    float controls[3][2] = {{120, 140}, {220, 220}, {float(logoDirections & 4 ? -80 : 170), 220}},
          colors[3][3] = {
              {32 / 128.f, 0, 111 / 128.f}, {0, 111 / 128.f, 32 / 128.f}, {111 / 128.f, 32 / 128.f, 0}};
    for (int layer = 0; layer < 3; layer++) {
        if (!((time >= 2.2f && time < 4.4f) || (layer == 0 && time >= 4.4f && time < 4.7f)))
            continue;
        float t = std::clamp((time - 2.2f) / durations[logoVariant][layer], 0.f, 1.f);
        int x = int((1 - t) * (1 - t) * starts[layer][0] + 2 * (1 - t) * t * controls[layer][0] +
                    t * t * (120 + logoParts.x)),
            y = int((1 - t) * (1 - t) * starts[layer][1] + 2 * (1 - t) * t * controls[layer][1] +
                    t * t * (220 + logoParts.y));
        // Same float expression per channel; its two factors are just computed once. A zero logo
        // alpha leaves the pixel unchanged exactly, so those pixels are skipped.
        float k[3];
        for (int c = 0; c < 3; c++)
            k[c] = time >= 4.4f ? 1.f : colors[layer][c] * t;
        for (int row = std::max(0, -y); row < std::min(logoParts.h, 600 - y); row++)
            for (int col = std::max(0, -x); col < std::min(logoParts.w, 800 - x); col++) {
                size_t a = (size_t(row) * logoParts.w + col) * 4, b = (size_t(row + y) * 800 + col + x) * 4;
                if (!logoParts.rgba[a + 3])
                    continue;
                float base = logoParts.rgba[a + 3] / 255.f;
                for (int c = 0; c < 3; c++) {
                    float alpha = base * k[c];
                    out.rgba[b + c] = uint8_t(out.rgba[b + c] * (1 - alpha) + logoParts.rgba[a + c] * alpha);
                }
            }
    }
    if (time >= 4.7f && time < 5.902f) {
        float phase = std::clamp((time - 4.7f) / .3f, 0.f, 4.f), levels[] = {128, 48, 16, 3, 0};
        int part = std::min(3, int(phase));
        float alpha = (levels[part] + (levels[part + 1] - levels[part]) * (phase - part)) / 128;
        auto fade = logoFinal;
        for (size_t i = 3; i < fade.rgba.size(); i += 4)
            fade.rgba[i] = uint8_t(fade.rgba[i] * alpha);
        blit(out, fade, 120 + fade.x, 220 + fade.y);
    }
    return out;
}
