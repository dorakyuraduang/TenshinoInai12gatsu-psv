#pragma once
#include "presentation.hpp"
#include <array>
#include <random>
namespace tenshi {
class OriginalWeather {
  public:
    struct Particle {
        float x = 0, y = 0, vx = 0, vy = 0, ax = 0, z = 0, dx = 0, dy = 0, alpha = 1;
        int delay = 0, depth = 0, frame = 0;
        bool started = false;
    };
    std::array<Particle, 120> particles{};
    int flags = 0, type = 0, density = 0, ticks = 0, phase = 0, windX = 0, windY = 0;
    double remainder = 0;
    std::mt19937 random{0x1245};
    int rand(int n) {
        return n <= 0 ? 0 : int(random() % unsigned(n));
    }
    void start(int f) {
        if (flags == f)
            return;
        flags = f;
        type = (f & 65535) >> 3;
        density = std::min(120, (f & 7) * (f & 7) * 20);
        ticks = phase = windX = windY = 0;
        remainder = 0;
        for (int i = 0; i < 120; i++) {
            particles[i] = {};
            particles[i].delay = i * (type == 2 ? 4 : type == 6 ? 3 : 8);
        }
    }
    void spawn(Particle &p, bool first) {
        p.started = true;
        p.alpha = 1;
        p.dx = p.dy = p.ax = p.vx = 0;
        if (type == 2 || type == 6) {
            int side = rand(4), distance = type == 6 ? 1500 : first ? 2000 : 100;
            p.x = side < 2 ? rand(1000) - 500 : side == 2 ? -400 - rand(distance) : 400 + rand(distance);
            p.y = side >= 2 ? rand(800) - 400 : side == 0 ? -300 - rand(distance) : 300 + rand(distance);
            p.z = type == 6 ? -rand(100) : 1000 + rand(first ? 1000 : 500);
            return;
        }
        p.x = type == 1 ? 100 + rand(900) : rand(800);
        p.y = -rand(400);
        p.depth = type == 1 ? rand(rand(rand(79) + 1) + 1) : rand(rand(79) + 1);
        if (type == 3) {
            int region = rand(7);
            p.x = region < 3 ? rand(200) : region > 3 ? 600 + rand(200) : 200 + rand(400);
            p.y = -rand(200);
            p.depth = rand(79) + 1;
        }
        if (type == 4) {
            p.x = (rand(2) == 0 ? 50 : 500) + rand(200);
            p.y = 200 - rand(100);
            p.depth = rand(80);
        }
        p.frame = type == 1   ? p.depth / 2
                  : type == 3 ? p.depth * 16 / 80
                  : type == 4 ? p.depth * 12 / 80
                              : p.depth * 20 / 80;
        p.vy = (type == 1   ? 7000 + p.depth * 300
                : type == 3 ? 1500 + p.depth * 150
                            : 5000 + p.depth * 200) /
               4096.f;
        if (type == 1)
            p.vx = (-2500 - p.depth * 120) / 4096.f;
        if (type == 4) {
            double angle = (63488 + int(p.x * 4096) / 304) * 6.283185307179586 / 65536;
            p.vy = float(std::cos(angle) * std::max(p.depth, 4) / 500);
            p.vx = float(std::sin(angle) * std::max(p.depth, 4) / 500);
        }
    }
    void tick(double delta) {
        if (!flags)
            return;
        remainder += delta * 60;
        int steps = int(remainder);
        remainder -= steps;
        for (int t = 0; t < steps; t++) {
            ticks++;
            if ((type == 2 || type == 6) && ticks % 4 == 0) {
                double angle = phase * 10 * 6.283185307179586 / 65536;
                int d = type == 6 ? 3000 : 300;
                windX = int(std::cos(angle) * 4096) / d;
                windY = int(std::sin(angle) * 4096) / d;
                phase += (type == 6 ? 20 : 200) + rand(20);
            } else if (ticks % 2 == 0) {
                windX = originalSin(phase / (type == 3 || type == 4 ? 3 : 2)) /
                        (type == 3 || type == 4 ? 30 : 23);
                phase += (1 + rand(10)) / 2;
            }
            for (int i = 0; i < density; i++) {
                auto &p = particles[i];
                if (p.delay > 0) {
                    p.delay--;
                    continue;
                }
                if (!p.started)
                    spawn(p, true);
                if (type == 2 || type == 6) {
                    p.z += type == 6 ? 1 + std::max(1, int(p.z) / 80) : -std::max(1, int(p.z) / 65);
                    if ((type == 2 && p.z <= 0) || (type == 6 && p.z >= 1000))
                        spawn(p, false);
                    p.dx += windX;
                    p.dy += windY;
                    p.x += int(p.dx) / 1200;
                    p.y += int(p.dy) / 1200;
                    p.frame = p.z > 1000 || p.z < 0 ? -1 : std::clamp(int(p.z) * 40 / 1000, 0, 39);
                    p.alpha = type == 6 ? std::clamp((1000 - p.z) / 500, 0.f, 1.f)
                                        : std::clamp(p.z / 400, 0.f, 1.f);
                } else {
                    if (ticks % 2 == 0) {
                        p.ax = (windX * (1 + p.depth / 8) / 2) / 4096.f;
                        if (p.y > (type == 4 ? 500 : 200))
                            p.alpha = std::max(1.f / 128, p.alpha - 3.f / 128);
                    }
                    p.vx += p.ax;
                    p.x += p.vx;
                    p.y += p.vy;
                    if (p.y > 600)
                        spawn(p, false);
                }
            }
        }
    }
};
} // namespace tenshi
