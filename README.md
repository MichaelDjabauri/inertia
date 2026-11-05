# Inertia

A toy 2D physics sandbox in C++20, with the solver written from scratch. It runs on the
desktop and, compiled to WebAssembly, in the browser.

**[Try it in your browser](https://michaeldjabauri.github.io/inertia/)**

This is a personal project made for fun, not a physics library. There's no API to build
on and no stability promises, and it's only been tested in Chrome on one Intel Mac.

![The tumbling drum with the settings panel open](docs/hero.jpg)

| | | |
|---|---|---|
| ![Chaos](docs/chaos.jpg) | ![Binary stars](docs/stars.jpg) | ![Wrecking ball](docs/wrecking.jpg) |
| ![Galton board](docs/galton.jpg) | ![Tumbling drum](docs/drum.jpg) | ![Jelly](docs/jelly.jpg) |

## What's in it

Twelve scenes, including a 136-box pyramid, Newton's Cradle, a wrecking ball, a Galton
board, orbiting stars, chaotic double pendulums, jelly and a self-driving rover.

| Input | Action |
|---|---|
| Click or drag | Use the selected tool: grab, throw, explode or pull |
| Right click | Remove a body |
| Scroll | Zoom |
| `1`–`9`, `0`, `-`, `=` | Choose a scene |
| `Space` / `R` | Pause / restart |
| `Tab` | Settings: gravity, time, bounce, substeps |
| `H` / `F` | Hide the interface / full screen |
| `←` `→` | Drive the rover |

## How it works

1. **Time.** Physics always advances in fixed steps of 1/60 s, however fast the screen
   refreshes. Drawing blends between the last two steps, so motion stays smooth.
2. **Collisions.** Sorting bounding boxes along one axis rules out most pairs cheaply.
   The separating axis theorem then finds the exact contacts for the rest. Contacts form
   slightly before bodies touch, which stops fast objects passing through walls.
3. **Solving.** This follows the soft step approach from Box2D v3. Each step is split into
   small substeps, and contacts and joints act as very stiff, damped springs. That stays
   stable where perfectly rigid constraints would overshoot.
4. **Sleeping.** Groups of touching bodies that have settled stop simulating until something
   hits them.
5. **Rendering.** Scenes draw into a 16-bit float target, so bright things can exceed white.
   Bloom and tone mapping turn that into the final image.
6. **The browser.** Emscripten compiles the same C++ to WebAssembly, and raylib draws through
   WebGL 2 instead of OpenGL. The whole site is about 2 MB.

## How accurate is it?

| Check | Theory | Measured |
|---|---|---|
| Newton's Cradle: height the far ball reaches | 100% | 99.9% |
| Galton Board: spread of the pile | 1.94 | 1.92 |
| Pendulum: energy error over 20 s | 0 | 0.12% |

A few scenes cheat a little. The Galton balls ignore each other until they reach the bins,
the cradle's balls have a tiny gap, and the rover has a gyro that helps it stay upright.

## Build and run

You need CMake 3.20+ and a C++20 compiler. CMake downloads raylib for you.

```sh
cmake -S . -B build
cmake --build build
./build/inertia
```

To build for the browser, install [Emscripten](https://emscripten.org/) and run:

```sh
emcmake cmake -S . -B build-web
cmake --build build-web
python3 -m http.server --directory build-web/site 8000
```

Then open <http://localhost:8000>. Pushes to `main` publish the site through
[GitHub Actions](.github/workflows/pages.yml).

## Tests

```sh
ctest --test-dir build       # physics and scene behaviour
tests/interaction_tests.sh   # drives the real app, and needs an awake display
```

## Credits

The solver follows Erin Catto's work on Box2D v3, and the bloom follows Jorge Jimenez's
Call of Duty: Advanced Warfare talk. Built with [raylib](https://www.raylib.com/). Fonts are
Inter and JetBrains Mono, both under the SIL Open Font License.
