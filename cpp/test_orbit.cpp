// Native physics test (no WASM/GL): verify the velocity-Verlet integrator the
// game uses keeps a circular orbit stable over many revolutions, including at
// coarse time-warp step sizes. Build: clang++ -std=c++17 cpp/test_orbit.cpp
#include <cstdio>
#include <cmath>
#include "world.h"

static int failures = 0;
static void check(bool ok, const char* msg) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", msg);
    if (!ok) failures++;
}

// Integrate a test particle under full N-body gravity with velocity Verlet
// (identical scheme to Game::integrate's coast path).
static void run(World& w, Vec2& p, Vec2& v, double dt, int steps, double t0) {
    double t = t0;
    for (int i = 0; i < steps; i++) {
        w.update(t);
        Vec2 a0 = w.gravityAt(p);
        Vec2 np = p + v * dt + a0 * (0.5 * dt * dt);
        w.update(t + dt);
        Vec2 a1 = w.gravityAt(np);
        v = v + (a0 + a1) * (0.5 * dt);
        p = np; t += dt;
    }
}

int main() {
    World w; w.build(); w.update(0);
    const Body& T = w.bodies[0];
    double mu = T.mu, R = T.radius;

    // --- circular low orbit, fine step, many revolutions ---
    {
        double r = R + 80000;
        double vc = std::sqrt(mu / r);
        double period = TAU * std::sqrt(r * r * r / mu);
        Vec2 p{r, 0}, v{0, vc};
        double rmin = 1e18, rmax = 0;
        int steps = (int)(period * 20 / (1.0 / 120));   // 20 orbits at 120 Hz
        double dt = 1.0 / 120;
        double t = 0;
        for (int i = 0; i < steps; i++) {
            run(w, p, v, dt, 1, t); t += dt;
            double rr = p.len();
            rmin = std::min(rmin, rr); rmax = std::max(rmax, rr);
        }
        double drift = (rmax - rmin) / r;
        printf("circular 80km, 20 orbits @120Hz: r=%.0f rmin=%.0f rmax=%.0f drift=%.4f%%\n",
               r, rmin, rmax, drift * 100);
        // bounded (third-body perturbation from Luna/Duna, not integrator decay)
        check(drift < 0.05, "low orbit radius bounded < 5% over 20 revolutions");
        check(p.len() > R, "still in orbit (did not decay into planet)");
    }

    // --- same orbit at coarse warp step (100x): must not blow up ---
    {
        double r = R + 200000;
        double vc = std::sqrt(mu / r);
        double period = TAU * std::sqrt(r * r * r / mu);
        Vec2 p{r, 0}, v{0, vc};
        double rmin = 1e18, rmax = 0;
        double dt = 1.0 / 120 * 100;   // warp 100x equivalent step
        int steps = (int)(period * 10 / dt);
        double t = 0;
        for (int i = 0; i < steps; i++) {
            run(w, p, v, dt, 1, t); t += dt;
            double rr = p.len();
            rmin = std::min(rmin, rr); rmax = std::max(rmax, rr);
        }
        double drift = (rmax - rmin) / r;
        printf("circular 200km, 10 orbits @warp-step: rmin=%.0f rmax=%.0f drift=%.4f%%\n",
               rmin, rmax, drift * 100);
        check(drift < 0.10, "warp-step orbit drift < 10% over 10 revolutions");
        check(std::isfinite(p.len()) && p.len() > R, "warp orbit finite and above surface");
    }

    // --- elliptic transfer toward Luna's distance: energy roughly conserved ---
    {
        double r = R + 100000;
        double vc = std::sqrt(mu / r) * 1.35;   // eccentric
        Vec2 p{r, 0}, v{0, vc};
        double e0 = 0.5 * vc * vc - mu / r;
        // half an orbit-ish
        run(w, p, v, 2.0, 4000, 0);
        double rr = p.len();
        double vv = v.len();
        double e1 = 0.5 * vv * vv - w.bodies[0].mu / (p - w.bodies[0].pos).len();
        double err = std::fabs((e1 - e0) / e0);
        printf("eccentric specific-energy drift over coast: %.4f%%\n", err * 100);
        check(err < 0.02, "specific orbital energy conserved < 2%");
    }

    // --- Luna position is well-defined and orbiting ---
    {
        w.update(0); Vec2 a = w.bodies[1].pos;
        w.update(20000); Vec2 b = w.bodies[1].pos;
        printf("Luna moved %.0f km in 20000 s\n", (b - a).len() / 1000);
        check((b - a).len() > 1000, "Luna actually orbits Terra");
        check(std::fabs((a - w.bodies[0].pos).len() - w.bodies[1].orbitR) < 1.0, "Luna stays at orbit radius");
    }

    printf("\n%s (%d failures)\n", failures ? "TESTS FAILED" : "ALL PHYSICS TESTS PASSED", failures);
    return failures ? 1 : 0;
}
