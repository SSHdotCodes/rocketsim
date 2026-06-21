# RocketSim

RocketSim is a 2D orbital-spaceflight sandbox in the spirit of **Spaceflight
Simulator** and **Kerbal Space Program**, but it runs entirely in the browser.
The game is written in **C++17** and compiled to **WebAssembly** with Emscripten;
it renders through WebGL2 and ships as ~135 KB of WASM with no runtime
dependencies. A tiny Node static server (`server.mjs`) serves the build.

## Play

Open the page and you start in the **hangar**:

- Build a rocket from the part palette (command pod, fuel tanks, liquid engines,
  solid boosters, decouplers, fins, parachute, landing legs, nose cones) or just
  fly the stock ship that's already on the pad.
- **LAUNCH**, then hold full throttle and steer **east (D)** to perform a gravity
  turn and fall into orbit. **SPACE** drops spent stages.
- Open the **MAP (M)** to read your orbit (apoapsis / periapsis) and time-warp
  (`.` / `,`) while coasting. Reach **Luna**, land on it (legs or low speed) and
  come home; deploy the **parachute (P)** to land in atmosphere.

Controls: `A`/`D` steer · `Shift`/`Ctrl` throttle · `Z`/`X` full/cut throttle ·
`Space` stage · `T` SAS · `P` parachute · `M` map · `,`/`.` time-warp · `R` revert.

### Simulation

- True N-body gravity (Terra + orbiting Luna + the far world Duna) integrated
  with velocity Verlet in double precision — real ascents, moon encounters,
  capture and landing, no patched-conic seams.
- Per-stage fuel, pressure-dependent engine Isp, atmospheric drag, aerodynamic
  fin stabilization, reaction-wheel + gimbal attitude control with SAS.
- Procedurally shaded planets (surface bands, day/night terminator, atmospheric
  halo) drawn with an SDF shader, plus additive exhaust/explosion particles.

## Build

Requires the [Emscripten SDK](https://emscripten.org) at `~/emsdk`.

```sh
./build.sh           # release build -> dist/ (rocketsim.js + .wasm + index.html)
./build.sh debug     # assertions build
npm start            # serve dist/ on http://127.0.0.1:4173 (PORT/HOST overridable)
```

The C++ lives in `cpp/` (`main.cpp` + headers); there is no toolchain on the
Pi, so always build locally and deploy `dist/`.

## Tests

- `clang++ -std=c++17 cpp/test_orbit.cpp -o /tmp/t && /tmp/t` — native physics
  test: orbit stability, energy conservation, warp-step robustness.
- `node flytest.mjs` / `node landtest.mjs` — headless Chrome autopilots that fly
  the stock ship to orbit and through a parachute landing, asserting the result.
