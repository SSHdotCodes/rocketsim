// Celestial bodies + N-body gravity on the rocket (a test particle).
// Terra is fixed at the origin (treated as the inertial frame). Luna orbits
// Terra analytically; Duna is a fixed far-away exploration target. This keeps
// integration numerically clean while still giving real moon encounters,
// capture, and landing.
#pragma once
#include <vector>
#include <string>
#include "math.h"

constexpr double G0 = 9.80665;

struct Body {
    std::string name;
    double mu;          // GM
    double radius;      // m
    double atmoH;       // atmosphere thickness, m (0 = airless)
    double rho0;        // sea-level density kg/m^3
    double scaleH;      // density scale height m
    Color col1, col2;   // surface highlight / base
    Color atmoCol;      // halo / sky tint
    float seed;
    bool isStar = false;
    int cost = 0;        // Astrobucks to unlock navigation/encounter data
    std::string desc;
    double soi = 0;      // optional gravity cutoff radius, 0 = always active
    // Circular orbit about parent (parentIdx < 0 => fixed at `fixedPos`).
    int parentIdx = -1;
    double orbitR = 0, orbitOmega = 0, phase0 = 0;
    Vec2 fixedPos{0, 0};
    // filled each frame:
    Vec2 pos{0, 0}, vel{0, 0};
};

struct World {
    std::vector<Body> bodies;
    Vec2 sunDir{0.95, 0.32};   // fixed light direction

    void build() {
        bodies.clear();
        // 0: Terra (home) — fixed at origin.
        Body terra;
        terra.name = "Terra"; terra.radius = 600000;
        terra.mu = G0 * terra.radius * terra.radius;        // g = 9.81 at surface
        terra.atmoH = 70000; terra.rho0 = 1.0; terra.scaleH = 6500;
        terra.col1 = rgb(0x6fae5b); terra.col2 = rgb(0x1f5fa6);
        terra.atmoCol = Color(0.45f, 0.68f, 1.0f, 0.9f); terra.seed = 1.3f;
        terra.parentIdx = -1; terra.fixedPos = {0, 0};
        bodies.push_back(terra);

        // 1: Luna — orbits Terra.
        Body luna;
        luna.name = "Luna"; luna.radius = 180000;
        luna.mu = 1.62 * luna.radius * luna.radius;          // g = 1.62
        luna.atmoH = 0; luna.rho0 = 0; luna.scaleH = 1;
        luna.col1 = rgb(0xd8d8e0); luna.col2 = rgb(0x8a8a96);
        luna.atmoCol = Color(0.7f, 0.7f, 0.8f, 0.25f); luna.seed = 4.7f;
        luna.parentIdx = 0; luna.orbitR = 12.0e6;
        luna.orbitOmega = std::sqrt(terra.mu / (luna.orbitR * luna.orbitR * luna.orbitR));
        luna.phase0 = 0.35;
        bodies.push_back(luna);

        // 2: Duna — fixed far target with a thin atmosphere.
        Body duna;
        duna.name = "Duna"; duna.radius = 320000;
        duna.mu = 2.9 * duna.radius * duna.radius;
        duna.atmoH = 38000; duna.rho0 = 0.22; duna.scaleH = 5800;
        duna.col1 = rgb(0xd98c5a); duna.col2 = rgb(0x8a3d24);
        duna.atmoCol = Color(0.9f, 0.55f, 0.4f, 0.6f); duna.seed = 9.1f;
        duna.parentIdx = -1; duna.fixedPos = {34.0e6, 9.0e6};
        bodies.push_back(duna);

        // Unlockable deep-space destinations. Distances and gravity are scaled
        // for gameplay, not real ephemerides; the map/navigation model stays
        // stable while making each world distinct.
        Body mercury;
        mercury.name = "Mercury"; mercury.radius = 220000;
        mercury.mu = 3.7 * mercury.radius * mercury.radius;
        mercury.atmoH = 0; mercury.rho0 = 0; mercury.scaleH = 1;
        mercury.col1 = rgb(0xb9ada0); mercury.col2 = rgb(0x6f665f);
        mercury.atmoCol = Color(0.45f, 0.43f, 0.40f, 0.18f); mercury.seed = 12.4f;
        mercury.parentIdx = -1; mercury.fixedPos = {-46.0e6, 14.0e6};
        mercury.cost = 3000; mercury.desc = "Small airless inner world";
        mercury.soi = 5.0e6;
        bodies.push_back(mercury);

        Body venus;
        venus.name = "Venus"; venus.radius = 520000;
        venus.mu = 8.6 * venus.radius * venus.radius;
        venus.atmoH = 110000; venus.rho0 = 2.1; venus.scaleH = 9200;
        venus.col1 = rgb(0xf2d28b); venus.col2 = rgb(0xa77535);
        venus.atmoCol = Color(1.0f, 0.72f, 0.36f, 0.95f); venus.seed = 18.6f;
        venus.parentIdx = -1; venus.fixedPos = {-19.0e6, -32.0e6};
        venus.cost = 5000; venus.desc = "Dense-atmosphere landing challenge";
        venus.soi = 8.0e6;
        bodies.push_back(venus);

        Body mars;
        mars.name = "Mars"; mars.radius = 340000;
        mars.mu = 3.7 * mars.radius * mars.radius;
        mars.atmoH = 46000; mars.rho0 = 0.18; mars.scaleH = 7000;
        mars.col1 = rgb(0xe0995c); mars.col2 = rgb(0x9b3f28);
        mars.atmoCol = Color(0.92f, 0.50f, 0.34f, 0.55f); mars.seed = 22.2f;
        mars.parentIdx = -1; mars.fixedPos = {58.0e6, -13.0e6};
        mars.cost = 6500; mars.desc = "Far red planet with thin air";
        mars.soi = 7.0e6;
        bodies.push_back(mars);

        Body sun;
        sun.name = "Sun"; sun.radius = 1200000;
        sun.mu = 18.0 * sun.radius * sun.radius;
        sun.atmoH = 220000; sun.rho0 = 0.05; sun.scaleH = 40000;
        sun.col1 = rgb(0xfff1a0); sun.col2 = rgb(0xff9b2f);
        sun.atmoCol = Color(1.0f, 0.55f, 0.08f, 1.0f); sun.seed = 31.5f;
        sun.isStar = true; sun.parentIdx = -1; sun.fixedPos = {-88.0e6, -22.0e6};
        sun.cost = 12000; sun.desc = "Extreme solar gravity and heat";
        sun.soi = 18.0e6;
        bodies.push_back(sun);

        Body jupiter;
        jupiter.name = "Jupiter"; jupiter.radius = 900000;
        jupiter.mu = 11.5 * jupiter.radius * jupiter.radius;
        jupiter.atmoH = 150000; jupiter.rho0 = 0.75; jupiter.scaleH = 24000;
        jupiter.col1 = rgb(0xe6c492); jupiter.col2 = rgb(0x8c6749);
        jupiter.atmoCol = Color(0.9f, 0.73f, 0.52f, 0.75f); jupiter.seed = 44.8f;
        jupiter.parentIdx = -1; jupiter.fixedPos = {96.0e6, 34.0e6};
        jupiter.cost = 15000; jupiter.desc = "Huge high-gravity gas giant";
        jupiter.soi = 16.0e6;
        bodies.push_back(jupiter);
    }

    void update(double t) {
        for (auto& b : bodies) {
            if (b.parentIdx < 0) { b.pos = b.fixedPos; b.vel = {0, 0}; continue; }
            const Body& p = bodies[b.parentIdx];
            double th = b.phase0 + b.orbitOmega * t;
            b.pos = p.pos + fromAngle(th, b.orbitR);
            b.vel = p.vel + Vec2{-std::sin(th), std::cos(th)} * (b.orbitOmega * b.orbitR);
        }
    }

    // gravity acceleration at world point p
    Vec2 gravityAt(Vec2 p) const {
        Vec2 a{0, 0};
        for (const auto& b : bodies) {
            Vec2 d = b.pos - p;
            double r2 = d.len2();
            double r = std::sqrt(r2);
            if (b.soi>0 && r>b.soi) continue;
            if (r < 1.0) continue;
            a += d * (b.mu / (r2 * r));
        }
        return a;
    }

    // index of body whose gravity dominates at p
    int dominant(Vec2 p) const {
        int best = 0; double bestA = -1;
        for (size_t i = 0; i < bodies.size(); i++) {
            Vec2 d = bodies[i].pos - p;
            double r = d.len();
            if (bodies[i].soi>0 && r>bodies[i].soi) continue;
            double r2 = std::max(d.len2(), 1.0);
            double a = bodies[i].mu / r2;
            if (a > bestA) { bestA = a; best = (int)i; }
        }
        return best;
    }

    double altitude(int bodyIdx, Vec2 p) const {
        return (p - bodies[bodyIdx].pos).len() - bodies[bodyIdx].radius;
    }

    // air density at p for the dominant atmosphere
    double density(int bodyIdx, Vec2 p) const {
        const Body& b = bodies[bodyIdx];
        if (b.atmoH <= 0) return 0;
        double alt = (p - b.pos).len() - b.radius;
        if (alt < 0) alt = 0;
        if (alt > b.atmoH) return 0;
        return b.rho0 * std::exp(-alt / b.scaleH);
    }
    // 0..1 pressure (for engine Isp)
    double pressure(int bodyIdx, Vec2 p) const {
        const Body& b = bodies[bodyIdx];
        if (b.atmoH <= 0 || b.rho0 <= 0) return 0;
        return clampd(density(bodyIdx, p) / b.rho0, 0.0, 1.0);
    }
};
