// RocketSim — C++/WebAssembly entry point. Wires Emscripten HTML5 input + the
// animation loop to the Game.
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstring>
#include "renderer.h"
#include "game.h"
#include "game_render.h"

static Renderer gRenderer;
static Game gGame;
static double gPrev = 0;

static void updateSize() {
    double cssW = 0, cssH = 0;
    emscripten_get_element_css_size("#canvas", &cssW, &cssH);
    if (cssW < 2 || cssH < 2) { cssW = 1280; cssH = 720; }
    double dpr = emscripten_get_device_pixel_ratio();
    if (dpr < 0.5) dpr = 1.0;
    emscripten_set_canvas_element_size("#canvas", (int)(cssW * dpr), (int)(cssH * dpr));
    gRenderer.resize((int)cssW, (int)cssH, (float)dpr);
}

static EM_BOOL onResize(int, const EmscriptenUiEvent*, void*) { updateSize(); return EM_TRUE; }

static bool isAction(const char* code, const char* want) { return std::strcmp(code, want) == 0; }

static EM_BOOL onKey(int type, const EmscriptenKeyboardEvent* e, void*) {
    Input& in = gGame.in;
    bool down = (type == EMSCRIPTEN_EVENT_KEYDOWN);
    const char* c = e->code;
    bool handled = true;
    if      (isAction(c, "KeyA") || isAction(c, "ArrowLeft"))  in.left = down;
    else if (isAction(c, "KeyD") || isAction(c, "ArrowRight")) in.right = down;
    else if (isAction(c, "ShiftLeft") || isAction(c, "ShiftRight") || isAction(c, "KeyW") || isAction(c, "ArrowUp")) in.throttleUp = down;
    else if (isAction(c, "ControlLeft") || isAction(c, "ControlRight") || isAction(c, "KeyS") || isAction(c, "ArrowDown")) in.throttleDown = down;
    else handled = false;

    if (down && !e->repeat) {
        if      (isAction(c, "KeyZ")) in.throttleMax = true;
        else if (isAction(c, "KeyX")) in.throttleZero = true;
        else if (isAction(c, "Space")) in.stage = true;
        else if (isAction(c, "KeyT")) in.sasToggle = true;
        else if (isAction(c, "KeyM")) in.mapToggle = true;
        else if (isAction(c, "KeyP")) in.deployChute = true;
        else if (isAction(c, "Period")) in.warpUp = true;
        else if (isAction(c, "Comma")) in.warpDown = true;
        else if (isAction(c, "KeyR")) in.revert = true;
        else if (isAction(c, "KeyH")) in.help = true;
        else if (isAction(c, "Enter")) in.launch = true;
        else return handled ? EM_TRUE : EM_FALSE;
        return EM_TRUE;
    }
    // swallow keys we use so the page doesn't scroll
    if (handled || isAction(c, "Space") || isAction(c, "ArrowUp") || isAction(c, "ArrowDown"))
        return EM_TRUE;
    return EM_FALSE;
}

static EM_BOOL onMouse(int type, const EmscriptenMouseEvent* e, void*) {
    Input& in = gGame.in;
    in.mx = e->targetX; in.my = e->targetY;
    if (type == EMSCRIPTEN_EVENT_MOUSEDOWN) {
        if (e->button == 2) in.rightPressed = true;
        else { in.mouseDown = true; in.mousePressed = true; }
    } else if (type == EMSCRIPTEN_EVENT_MOUSEUP) {
        if (e->button != 2) { in.mouseDown = false; in.mouseReleased = true; }
    }
    return EM_TRUE;
}

static EM_BOOL onWheel(int, const EmscriptenWheelEvent* e, void*) {
    gGame.in.wheel += (e->deltaY < 0) ? 1.0 : -1.0;
    return EM_TRUE;
}

static void tick() {
    double now = emscripten_get_now();
    double dt = (now - gPrev) / 1000.0;
    gPrev = now;
    if (dt < 0 || dt > 0.25) dt = 1.0 / 60.0;

    gGame.step(dt);
    gr::render(gGame);

    // publish flight telemetry to JS (debugging / automated verification)
    if (gGame.mode == MODE_FLIGHT) {
        const Body& B = gGame.world.bodies[gGame.domBody];
        Vec2 up = (gGame.pos - B.pos).norm();
        Vec2 vrel = gGame.vel - B.vel;
        double alt = (gGame.pos - B.pos).len() - B.radius;
        double fuel = 0; for (double f : gGame.sectionFuel) fuel += f;
        Vec2 nose = gGame.noseDir();
        double pitch = std::acos(clampd(nose.dot(up), -1, 1)) * RAD2DEG;  // 0=up,90=horizon
        Vec2 vd = vrel.len() > 1 ? vrel.norm() : nose;
        double progradeErr = std::atan2(nose.cross(vd), nose.dot(vd)) * RAD2DEG; // signed nose->velocity
        double srb = 0; for (auto& p : gGame.parts) if (p.alive && spec(p.type).isSRB) srb += p.srbFuel;
        double soundThrust = 0;
        for (auto& p : gGame.parts) if (p.alive && p.engineOn) {
            const PartSpec& s = spec(p.type);
            if (s.isSRB && p.srbFuel > 0) soundThrust += s.thrust;
            else if (s.isEngine && gGame.enginesEnabled && p.section < gGame.nSections &&
                     gGame.sectionFuel[p.section] > 0 && gGame.throttle > 0.02)
                soundThrust += s.thrust * gGame.throttle;
        }
        double engineLevel = clampd(1.0 - std::exp(-soundThrust / 700000.0), 0.0, 1.0);
        EM_ASM({ window.__rs = ({ mode:1, alt:$0, apo:$1, peri:$2, spd:$3, vspd:$4,
            body:UTF8ToString($5), landed:$6, exploded:$7, stage:$8, nstage:$9, fuel:$10, warp:$11,
            pitch:$12, srb:$13, progradeErr:$14, heat:$15 }); },
            alt, gGame.hyperbolic ? 1e12 : gGame.apo, gGame.peri, vrel.len(), vrel.dot(up),
            B.name.c_str(), gGame.landed ? 1 : 0, gGame.exploded ? 1 : 0,
            gGame.currentStage, gGame.nStages, fuel, gGame.warps[gGame.warpIdx], pitch, srb, progradeErr, gGame.reentryHeat);
        EM_ASM({ if (window.__rs) window.__rs.target = $0; }, gGame.targetBody);
        EM_ASM({ if (window.__rsAudioUpdate) window.__rsAudioUpdate($0, $1, $2, $3, $4); },
            engineLevel, gGame.reentryHeat, gGame.exploded ? 1 : 0, gGame.landed ? 1 : 0, gGame.mode == MODE_FLIGHT ? 1 : 0);
    } else {
        EM_ASM({ window.__rs = ({ mode:0, target:$0 }); }, gGame.targetBody);
        EM_ASM({ if (window.__rsAudioUpdate) window.__rsAudioUpdate(0, 0, 0, 0, 0); });
    }

    // consume one-frame edge inputs
    Input& in = gGame.in;
    in.mousePressed = in.mouseReleased = in.rightPressed = false;
    in.wheel = 0;
    in.stage = in.sasToggle = in.mapToggle = in.deployChute = false;
    in.warpUp = in.warpDown = in.launch = in.revert = in.help = false;
    in.throttleMax = in.throttleZero = false;
}

int main() {
    if (!gRenderer.init()) { printf("renderer init failed\n"); return 1; }
    updateSize();
    gGame.init(&gRenderer);
    gPrev = emscripten_get_now();

    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_FALSE, onResize);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_FALSE, onKey);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_FALSE, onKey);
    emscripten_set_mousedown_callback("#canvas", nullptr, EM_FALSE, onMouse);
    emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_FALSE, onMouse);
    emscripten_set_mousemove_callback("#canvas", nullptr, EM_FALSE, onMouse);
    emscripten_set_wheel_callback("#canvas", nullptr, EM_FALSE, onWheel);

    emscripten_set_main_loop(tick, 0, 1);
    return 0;
}
