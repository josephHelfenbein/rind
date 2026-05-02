# Rind

## Prerequisites

### General
- **CMake** 3.10+
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

- **Option A — LunarG Vulkan SDK (recommended):** Download and run the installer from [vulkan.lunarg.com](https://vulkan.lunarg.com/sdk/home#mac). The SDK bundles DXC and sets `VULKAN_SDK` automatically, which CMake will pick up. If you use this, you can skip `molten-vk`, `vulkan-loader`, and `vulkan-headers` from the Homebrew step above.

- **Option B — Homebrew tap:**
  ```bash
  brew tap SharkyRawr/dxc
  brew install SharkyRawr/dxc/directx-shader-compiler
  ```

---

### Linux — Ubuntu

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

### Linux — Debian

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

### Linux — Arch

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

Optionally place a 256×256 PNG at `cmake/rind.png` to embed an icon.

### Windows

No extra tools required. A `.zip` of the distributable folder is created automatically using CMake's built-in archive support.

---

## Notes
- Shaders are automatically compiled from HLSL to SPIR-V using `dxc` during the build process.
- Assets are expected to be in `src/assets`.
- Vulkan 1.3 with Dynamic Rendering support is required. GPUs that only support Vulkan 1.1 or 1.2 will not run the application.