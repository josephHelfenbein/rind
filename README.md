# Rind

Rind is an open-source first-person shooter and the custom Vulkan engine it runs on. You fight waves of robots, and the difficulty gets harder the longer you stay alive.

Licensed under Apache 2.0.

## About the game

You play a robot with a laser gun, grenades, a melee punch, a dash, and a double jump. The laser gun can overheat, so timing is important. Enemy robots arrive in waves that scale with difficulty: walking grunts, flying drones, and heavy bashers, plus four elite variants (flying, bashing, grenade, missile) with their own attack patterns. An enemy kill can give a random status effect that buffs or debuffs the player. The player can also heal from the explosion left behind when killing an enemy. The score will keep climbing until you lose.

## About this repository

This repository is the source build of Rind. The codebase is open so people can read the code, contribute, mod the game, or build something of their own on top of the engine. The Steam page can be found [here](https://store.steampowered.com/app/4412940/Rind/).

## Prerequisites

### General
- **CMake** 3.21+
- **Vulkan SDK** 1.3+ (must support Dynamic Rendering)
- **C++20** compatible compiler
- **DXC** (DirectX Shader Compiler), usually included with the Vulkan SDK

---

### Windows

1. Install [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **"Desktop development with C++"** workload selected. This provides MSVC, CMake, and the Windows SDK.
2. Install the [LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home#windows). This includes `dxc.exe` and sets up `VULKAN_SDK` in your environment automatically.
3. Verify `dxc.exe` is accessible. It should be on your PATH after the SDK install. If not, add `%VULKAN_SDK%\Bin` to your PATH manually.

> OpenMP is provided by MSVC's built-in LLVM OpenMP runtime, so no separate install is required.

---

### macOS

macOS requires MoltenVK to run Vulkan on Metal. You can get all dependencies either via the **LunarG Vulkan SDK** (recommended, includes DXC) or via **Homebrew** with a separate DXC install.

**1. Install Xcode Command Line Tools**
```bash
xcode-select --install
```

**2. Install Homebrew** (if not already installed)
```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

**3. Install dependencies**
```bash
brew install cmake glfw freetype libomp molten-vk vulkan-loader vulkan-headers
```

- `molten-vk` - Vulkan implementation on top of Metal (required on macOS)
- `vulkan-loader` - Vulkan loader library (`libvulkan`)
- `vulkan-headers` - Vulkan header files
- `glfw` - windowing library
- `freetype` - font rendering library
- `libomp` - LLVM OpenMP runtime (used for CPU parallelism)

**4. Install DXC (DirectX Shader Compiler)**

DXC is not in Homebrew core. Choose one of:

- **Option A, LunarG Vulkan SDK (recommended):** Download and run the installer from [vulkan.lunarg.com](https://vulkan.lunarg.com/sdk/home#mac). The SDK bundles DXC and sets `VULKAN_SDK` automatically, which CMake will pick up. If you use this, you can skip `molten-vk`, `vulkan-loader`, and `vulkan-headers` from the Homebrew step above.

- **Option B, Homebrew tap:**
  ```bash
  brew tap SharkyRawr/dxc
  brew install SharkyRawr/dxc/directx-shader-compiler
  ```

---

### Linux: Ubuntu

For Ubuntu 24.04 (Noble) or Ubuntu 22.04 (Jammy).

**1. Install build tools and dependencies**
```bash
sudo apt install \
  build-essential cmake git \
  libvulkan-dev \
  libglfw3-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libomp-dev
```

For **Ubuntu 24.04 (Noble)**:
```bash
sudo apt install libfreetype-dev
```

For **Ubuntu 22.04 (Jammy)**:
```bash
sudo apt install libfreetype6-dev
```

**2. Install DXC via LunarG apt repo**

DXC is not in Ubuntu's standard repositories. Install it from the LunarG apt repo:

For **Ubuntu 24.04 (Noble)**:
```bash
wget -qO- https://packages.lunarg.com/lunarg-signing-key-pub.asc \
  | sudo tee /etc/apt/trusted.gpg.d/lunarg.asc
sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-noble.list \
  https://packages.lunarg.com/vulkan/lunarg-vulkan-noble.list
sudo apt update
sudo apt install dxc
```

For **Ubuntu 22.04 (Jammy)**, replace `noble` with `jammy` in both URLs above.

> Alternatively, download the full [LunarG Linux tarball](https://vulkan.lunarg.com/sdk/home#linux) which bundles DXC alongside the entire Vulkan SDK and sets up `VULKAN_SDK` for you.

---

### Linux: Debian

For Debian 12 (Bookworm).

**1. Install build tools and dependencies**
```bash
sudo apt install \
  build-essential cmake git \
  libvulkan-dev \
  libglfw3-dev \
  libfreetype6-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libomp-dev
```

**2. Install DXC**

There is no LunarG apt repo for Debian. Download the pre-built DXC binary from the [DXC GitHub releases](https://github.com/microsoft/DirectXShaderCompiler/releases):

```bash
# Replace the filename with the latest release version
wget https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2602/linux_dxc_2026_02_20.x86_64.tar.gz
tar -xf linux_dxc_*.x86_64.tar.gz
sudo cp bin/dxc /usr/local/bin/dxc
sudo chmod +x /usr/local/bin/dxc
```

> Alternatively, download the full [LunarG Linux tarball](https://vulkan.lunarg.com/sdk/home#linux) which bundles DXC with the full Vulkan SDK.

---

### Linux: Arch

**1. Install all dependencies**
```bash
sudo pacman -S \
  base-devel cmake git \
  vulkan-headers vulkan-icd-loader \
  glfw \
  freetype2 \
  openmp \
  directx-shader-compiler
```

- `vulkan-icd-loader` - Vulkan loader (runtime). You also need a Vulkan ICD driver for your GPU:
  - NVIDIA: `nvidia-utils`
  - AMD: `vulkan-radeon` (RADV, open-source) or `amdvlk`
  - Intel: `vulkan-intel`
- `glfw` - provides both X11 and Wayland support
- `freetype2` - font rendering library
- `openmp` - OpenMP runtime (companion to GCC's built-in support)
- `directx-shader-compiler` - DXC, available in the official `extra` repo

> You may also want `vulkan-validation-layers` for debugging.

---

## Build Instructions

### 1. Clone the repository
```bash
git clone --recursive https://github.com/josephHelfenbein/rind.git
cd rind
```

If already cloned, run
```bash
cd rind
git submodule update --init --recursive
```

### 2. Configure

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

### 3. Build

```bash
cmake --build build --config Release --parallel
```

### 4. Run

The executable will be located in the `bin` directory (or `bin/Release` on Windows).

**Windows:**
```powershell
.\bin\Release\Rind.exe
```

**macOS / Linux:**
```bash
./bin/Rind
```

## Packaging

After a successful Release build, run the packaging script to produce a platform-native distributable:

```bash
cmake -P cmake/package.cmake
```

Output is written to a `dist/` folder in the project root. An optional version string can be passed:

```bash
cmake -DVERSION=1.2.0 -P cmake/package.cmake
```

The script auto-detects the OS it is running on and packages accordingly.

| Platform | Output |
|----------|--------|
| macOS    | `dist/Rind.app` |
| Linux    | `dist/Rind-<version>.AppImage` |
| Windows  | `dist/Rind-<version>-windows/` folder + `.zip` |

### macOS

No extra tools required. The `.app` bundle includes the bundled dylibs and Vulkan ICD and can be double-clicked or distributed as-is. Optionally place a `cmake/Rind.icns` file to embed an icon.

### Linux

Requires `appimagetool` on PATH. Download the latest release from [AppImageKit](https://github.com/AppImage/appimagetool/releases/tag/continuous):

```bash
chmod +x appimagetool-x86_64.AppImage
sudo mv appimagetool-x86_64.AppImage /usr/local/bin/appimagetool
```

Optionally place a 256x256 PNG at `cmake/rind.png` to embed an icon.

### Windows

No extra tools required. A `.zip` of the distributable folder is created automatically using CMake's built-in archive support.

---

## Repository layout

- **`src/engine/`** and **`include/engine/`**: the engine, built as the static library target `rind_engine`. Renderer, shader and render-graph manager, entity and scene managers, asset managers, audio, input, UI, lighting, particles, volumetrics, collision. Engine-owned shaders live in `src/engine/assets/shaders/hlsl/` and compile into the library. Nothing in here knows about the game.
- **`src/rind/`**: the game, built as the `Rind` executable that links against `rind_engine`. Character controller, player logic, enemy AI, elite variants, projectiles, status effects, spawners, score tracking, and the top-level `GameInstance` that wires it all together. Game headers live in `src/rind/include/rind/`. Game-only shaders go in `src/rind/assets/shaders/hlsl/`, which is empty by default since Rind currently has no game-specific shaders.
- **`src/assets/`**: game-owned non-shader assets: `models/`, `textures/`, `audio/`, `fonts/`. These are embedded into the `Rind` executable, not into the engine library, and registered with the engine managers at startup via `registerEmbedded*()` calls in `GameInstance`.
- **`src/main.cpp`**: process entry point. Six lines: calls `engine::Platform::initialize()` (macOS Vulkan ICD bootstrap, no-op elsewhere) and `engine::Platform::runWithCrashReport(...)` with a lambda that constructs and runs `rind::GameInstance`.
- **`cmake/`**: `RindEngine.cmake` exports the build helpers consumers need (`embed_asset_category`, `rind_engine_compile_shaders`, `rind_engine_bundle_runtimes`); `embed_asset.py` and `generate_registry.py` are the worker scripts those helpers invoke; `package.cmake` builds release artifacts.
- **`include/external/`**: vendored third-party libraries pulled in as submodules.

## Engine overview

The engine is a deferred PBR renderer built on Vulkan 1.3 with Dynamic Rendering. The renderer owns the shared GPU state and every other manager asks it for resources or hands work back to it. Headers are in `include/engine/` and sources are in `src/engine/`.

- **Renderer** - Vulkan host: instance, device, queues, swapchain, command pools, descriptor allocators, and per-frame command recording.
- **ShaderManager** - Shader modules, render passes, and the render graph. Passes are `RenderNode`s organized into `RenderLane`s; async-capable lanes run on a separate compute queue in parallel with graphics. The current graph runs five lanes: `GeneralGraphics`, `Volumetric`, `Shadow`, `IrradianceSH`, and `IrradianceRender`.
- **LightManager** - Up to 16 point lights, each with a static baked shadow cubemap and a per-frame dynamic cubemap for moving lights.
- **IrradianceManager** - Up to 64 irradiance probes with baked color cubemaps and dynamic cubemaps projected to spherical harmonics for indirect lighting. Runs on its own async lanes.
- **ParticleManager** - CPU-side particle pool with lifetime, velocity, and collision simulation, written to a persistently mapped GPU buffer.
- **VolumetricManager** - Smoke, muzzle flash, and explosion volumes with lifetime easing.
- **EntityManager / SceneManager** - Hierarchical entity tree with transform inheritance, skeletal animation, and colliders. SceneManager swaps between top-level scenes.
- **ModelManager** - glTF 2.0 loading via `fastgltf`, GPU buffers, skeleton and animation data.
- **TextureManager** - Image resources for materials, UI, render targets, and HDR environment maps.
- **Collider / SpatialGrid** - AABB, OBB, and convex-hull SAT tests, broad-phased by a 3D hash grid.
- **AudioManager** - `miniaudio` wrapper with 3D spatialization and pitch variation.
- **InputManager** - GLFW keyboard, mouse, and gamepad input. Controller mode provides on-screen cursor navigation for menus.
- **UIManager** - 2D overlay with `FreeType` glyph caching and anchored widget layout.
- **Camera** - Perspective camera with frustum culling.
- **SettingsManager** - Persistent video, audio, and input settings.

## Rendering pipeline

The graph is described in `ShaderManager::createDefaultShaders`. One frame runs roughly in this order, with the lanes noted in parentheses indicating where work can run in parallel.

1. **G-buffer** (GeneralGraphics). Writes albedo, normal, depth, and material parameters for every renderable 3D entity.
2. **AO** and **Volumetric** (graphics + Volumetric lane). Both depend only on the G-buffer and run concurrently with the work below. AO uses a compute shader (`ao.comp.hlsl`). Each volumetric effect is a cube mesh; the fragment shader ray-marches from the camera through the cube, sampling a curl-noise density field built with FBM for turbulence, fading by age, and clipping against scene depth. The pass uses adaptive step refinement and per-frame rejitter.
3. **Shadow preparation, image generation, and blur** (Shadow lane, async). The scene is rendered into point-light cubemaps that store linear depth from the light. A compute pass (`shadowimage.comp.hlsl`) reads those cubemaps and writes a soft-shadow image atlas using PCSS: a blocker search on a world-space-rotated disk pattern, a penumbra estimate from the blocker depths, then a second filtered sample pass at the computed radius. Two compute passes (`blurarray.comp.hlsl`) then apply a bilateral, depth- and normal-aware blur horizontally and vertically. The whole chain rides the Shadow lane so it does not block the G-buffer or lighting.
4. **Irradiance probe update** (IrradianceRender then IrradianceSH lanes, async). Each probe's dynamic cubemap is re-rendered with simplified particles, finalized, projected to spherical harmonics, and reduced. The baked color cubemap from scene load is left alone. The whole chain runs on its own lanes in parallel with the graphics work above.
5. **Particles** (GeneralGraphics). CPU simulation then draw from persistently mapped GPU buffer, depends on the G-buffer for depth-aware scaling and occlusion.
6. **Lighting** (GeneralGraphics). Consumes the G-buffer, shadow image, AO, volumetric contribution, particle contribution, and probe SH coefficients to produce the lit HDR image. This is the synchronization point where all the async lanes rejoin the main lane.
7. **SSR**. Reads the lit image and the G-buffer, ray-marches against depth, and produces a roughness-aware screen-space reflection contribution.
8. **Bloom** down-sample chain then **bloom** up-sample chain. Reads the lit image and produces a bloom blur pyramid.
9. **Flare**. Reads a mid-pyramid bloom mip and produces lens flare contributions for very bright pixels.
10. **Combine**. Mixes lighting, SSR, the bloom up-sample, and flare into a single HDR image.
11. **SMAA** edge detection, then weight calculation, then blend. Three sequential passes that produce the anti-aliased image when enabled.
12. **UI**. Renders the 2D overlay into its own image. Has no graph dependencies and can record alongside earlier passes.
13. **Composite**. Final pass. Depends on combine, ui, and smaa_blend. Mixes the anti-aliased lit image with the UI overlay, dithers to the swapchain format, and produces the presentable frame.

Skip conditions are attached to nodes that can be empty (no renderable 3D entities, no particles, no probes needing update), so quiet frames are cheaper than busy ones.

## Game code

All gameplay lives in `src/rind/`. The engine has no knowledge of these files; replacing them is how you would build a different game on the engine.

`GameInstance` is the top-level controller that owns the engine managers, builds scenes, registers spawners, and drives the main loop. The player is a first-person `CharacterEntity` with a laser gun, grenades, melee, dash, and double jump. Enemies (`WalkingEnemy`, `FlyingEnemy`, `BashingEnemy`) extend a shared `Enemy` subclass of `CharacterEntity` with a state machine for spawning, chasing, and attacking. Four elite variants add dashes, grenades, or guided missiles. `EnemySpawner` is a templated wave spawner that scales difficulty along a sinusoidal curve.

## Asset pipeline

Assets are embedded into the binary at build time, so a shipped executable needs no external asset folder. The `embed_asset_category` CMake function (in `cmake/RindEngine.cmake`) scans a directory for matching files, generates one `.cpp` per file holding the raw bytes, and emits a per-category `_registry.h` exposing a `getEmbedded_<category>()` lookup map. Each call attaches the generated sources to a CMake target you specify.

Categories used today:

| Category | Owner | Target | Extensions |
| --- | --- | --- | --- |
| `shader` | engine | `rind_engine` | `.spv` (HLSL compiled by dxc) |
| `game_shader` | game | `Rind` | `.spv` (HLSL compiled by dxc) |
| `font` | game | `Rind` | `.ttf`, `.otf` |
| `audio` | game | `Rind` | `.wav` |
| `model` | game | `Rind` | `.glb` |
| `texture` | game | `Rind` | `.png`, `.jpg`, `.hdr` |

Engine shaders are baked into `librind_engine.a` at engine-build time. Every other category is consumer-owned: the game compiles its own assets into its executable and hands them to the engine managers at startup via runtime registration (`shaderManager->registerShaderBytes(...)`, `textureManager->registerEmbeddedTextures(...)`, etc.). This keeps the engine library free of any compile-time dependency on the consumer's assets.

## Using `rind_engine` as a submodule

A downstream project that wants to build a different game on this engine adds Rind as a submodule and links against the `rind_engine` static library. A minimal consumer `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.21)
project(MyGame)

add_subdirectory(third_party/rind)

add_executable(MyGame src/main.cpp src/MyGameInstance.cpp)
target_include_directories(MyGame PRIVATE src)
target_link_libraries(MyGame PRIVATE rind_engine)

# Compile and embed your own shaders. Both graphics (.vert/.frag) and
# compute (.comp) HLSL files are picked up by stem-based stage detection.
rind_engine_compile_shaders(
    TARGET MyGame
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/assets/shaders/hlsl
    OUT_DIR    ${CMAKE_BINARY_DIR}/shaders/compiled)
embed_asset_category(TARGET MyGame CATEGORY game_shader
    DIRECTORY ${CMAKE_BINARY_DIR}/shaders/compiled
    EXTENSIONS "*.spv")

# Embed your own non-shader assets.
embed_asset_category(TARGET MyGame CATEGORY texture
    DIRECTORY ${CMAKE_SOURCE_DIR}/assets/textures
    EXTENSIONS "*.png" "*.hdr" RECURSIVE ON)
embed_asset_category(TARGET MyGame CATEGORY font
    DIRECTORY ${CMAKE_SOURCE_DIR}/assets/fonts
    EXTENSIONS "*.ttf" "*.otf" RECURSIVE ON)
embed_asset_category(TARGET MyGame CATEGORY audio
    DIRECTORY ${CMAKE_SOURCE_DIR}/assets/audio
    EXTENSIONS "*.wav")
embed_asset_category(TARGET MyGame CATEGORY model
    DIRECTORY ${CMAKE_SOURCE_DIR}/assets/models
    EXTENSIONS "*.glb" RECURSIVE ON)

# Platform runtime bundling: copies Vulkan loader / MoltenVK / libomp / etc.
# next to the executable for Win/macOS/Linux release builds.
rind_engine_bundle_runtimes(MyGame)
```

A minimal consumer `main.cpp`:

```cpp
#include <engine/Platform.h>
#include "MyGameInstance.h"

int main() {
    engine::Platform::initialize();
    return engine::Platform::runWithCrashReport([] {
        mygame::MyGameInstance game;
        game.run();
    });
}
```

A minimal `MyGameInstance`, mirroring the wiring `rind::GameInstance` does:

```cpp
// Generated by embed_asset_category() for each category embedded above
#include <texture/texture_registry.h>
#include <font/font_registry.h>
#include <audio/audio_registry.h>
#include <model/model_registry.h>
#include <game_shader/game_shader_registry.h>

mygame::MyGameInstance::MyGameInstance() {
    renderer        = std::make_unique<engine::Renderer>("MyGame");
    entityManager   = std::make_unique<engine::EntityManager>(renderer.get());
    // ... other managers ...
    textureManager  = std::make_unique<engine::TextureManager>(renderer.get());
    shaderManager   = std::make_unique<engine::ShaderManager>(renderer.get());
    uiManager       = std::make_unique<engine::UIManager>(renderer.get());
    modelManager    = std::make_unique<engine::ModelManager>(renderer.get());
    audioManager    = std::make_unique<engine::AudioManager>(renderer.get());

    // Hand consumer-side embedded assets to engine managers BEFORE run()
    shaderManager->registerShaderBytes(getEmbedded_game_shader());
    audioManager->registerEmbeddedAudio(getEmbedded_audio());
    textureManager->registerEmbeddedTextures(getEmbedded_texture());
    modelManager->registerEmbeddedModels(getEmbedded_model());
    uiManager->registerEmbeddedFonts(getEmbedded_font());

    // Optional: change the font that engine-internal widgets request
    // uiManager->setDefaultFontName("MyFont");
}
```

### Extending the engine: shader override and render-graph mutation

`ShaderManager` exposes hooks for changing what the default render graph does without forking the engine:

- `registerShaderBytes(name, data, size)` and the batch overload register additional shaders, or override one of the engine's defaults. Registered entries are checked before the engine's embedded shader registry, so a game can ship a `lighting.frag` (or `gbuffer.vert`, or any other named shader) that supersedes the engine's.
- `setOnRenderGraphReady(callback)` is invoked at the top of `resolveRenderGraphShaders`, between the engine's `createDefaultShaders()` building the graph and the scheduler resolving it. Use this to mutate the graph.
- `replaceRenderNode(name, newNode)`, `insertRenderNodeAfter(predecessor, newNode)`, and `removeRenderNode(name)` do surgical edits to the render graph by node name.

### Known engine couplings to be aware of

The engine bakes in some assumptions that consumers should know about:

- **Default fonts.** The engine's built-in settings UI body text, FPS counter, and slider value labels request a font by the name set on `UIManager::setDefaultFontName(...)` (default `"Lato"`). The settings panel title uses `UIManager::setDefaultTitleFontName(...)` (default `"RubikGlitch"`). Register a font under whatever names you set, or change the defaults before any engine-internal widget is created.
- **Material fallback names.** `EntityManager` falls back to texture names `materials_default_albedo`/`_metallic`/`_roughness`/`_normal` when a `gbuffer` entity's specified textures are missing, and to `ui_window` for missing UI textures. Missing entities just log a warning; they don't crash.
- **SMAA texture names.** The engine's SMAA pass expects textures named `smaa_area` and `smaa_search` to be auto-created from the SMAA submodule's baked tables; this is handled internally and shouldn't need attention from consumers.

## Contributing

Contributions are welcome under Apache 2.0. Issues and pull requests live on [GitHub](https://github.com/josephHelfenbein/rind).

Notes:

- For a debug build, configure with `-DCMAKE_BUILD_TYPE=Debug`. Vulkan validation layers are not enabled by default; install the Vulkan SDK's validation layers and set `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` (or the loader's `VK_LOADER_LAYERS_ENABLE` equivalent) in the environment when running.
- Shader changes recompile from HLSL to SPIR-V automatically. If a shader fails to compile, the build error will name the source file.
- The engine and game sources are picked up by `file(GLOB_RECURSE ... src/engine/*.cpp)` into the `rind_engine` library target, and `file(GLOB_RECURSE ... src/rind/*.cpp)` into the `Rind` executable, both in the root `CMakeLists.txt`. New source files in those directories are picked up on the next reconfigure.
- New engine shaders go in `src/engine/assets/shaders/hlsl/`; new game shaders go in `src/rind/assets/shaders/hlsl/`. Both are picked up automatically and compiled to SPIR-V by `rind_engine_compile_shaders` on the next configure. Stem-based stage detection: `*.vert.hlsl` maps to vertex, `*.frag.hlsl` to fragment, `*.comp.hlsl` to compute.
- The render graph is data-driven. New passes are added by appending a `RenderNode` in `ShaderManager::createDefaultShaders` with the right `dependsOnNodeNames` and lane assignment; there is no separate scheduler file to update. Consumer projects that don't want to fork the engine can add or replace passes from outside via `setOnRenderGraphReady` plus `insertRenderNodeAfter` or `replaceRenderNode` (see "Extending the engine" above).

## Acknowledgements

Third-party libraries Rind depends on:

- [glm](https://github.com/g-truc/glm), linear algebra
- [GLFW](https://www.glfw.org), windowing and input
- [FreeType](https://freetype.org), font rasterization
- [fastgltf](https://github.com/spnda/fastgltf), glTF parsing
- [miniaudio](https://github.com/mackron/miniaudio), audio playback
- [stb](https://github.com/nothings/stb), image decoding
- [SMAA](https://github.com/iryoku/smaa), anti-aliasing
- [DXC](https://github.com/microsoft/DirectXShaderCompiler), HLSL to SPIR-V compilation
- [MoltenVK](https://github.com/KhronosGroup/MoltenVK), Vulkan on Metal for macOS

## License

Apache License 2.0. See [`LICENSE`](LICENSE).
