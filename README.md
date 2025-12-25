# Rind

## Prerequisites

### General
- **CMake** 3.10+
- **Vulkan SDK** 1.3+ (Must support Dynamic Rendering)
- **C++20** compatible compiler
- **DXC** (DirectX Shader Compiler), usually included with Vulkan SDK

### Windows
1. Install [Visual Studio 2022](https://visualstudio.microsoft.com/) with "Desktop development with C++".
2. Install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home#windows).
3. Ensure `dxc.exe` is in your PATH (usually added by Vulkan SDK).

### MacOS
1. Install Xcode and Command Line Tools.
2. Install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home#mac).
3. Install CMake (e.g., via Homebrew: `brew install cmake`).
4. Ensure `dxc` is available.

### Linux
1. Install build tools (GCC/Clang), CMake, and Git.
2. Install Vulkan SDK (follow instructions at [LunarG](https://vulkan.lunarg.com/sdk/home#linux)).
3. Install system development libraries required by GLFW (package names vary by distro):
   - X11 (libX11)
   - XRandR (libXrandr)
   - XInerama (libXinerama)
   - XCursor (libXcursor)
   - XInput (libXi)

   *Example (Ubuntu/Debian):*
   ```bash
   sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
   ```

## Build Instructions

### 1. Clone the repository
```bash
git clone --recursive https://github.com/josephHelfenbein/rind.git
cd rind
```

### 2. Configure
**Windows (PowerShell):**
```powershell
cmake -S . -B build
```

**MacOS / Linux:**
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

### 3. Build
**Windows (PowerShell):**
```powershell
cmake --build build --config Release --parallel
```

**MacOS / Linux:**
```bash
cmake --build build --parallel
```

### 4. Run
The executable will be located in the `bin` directory (or `bin/Release` on Windows).

**Windows:**
```powershell
.\bin\Release\Rind.exe
```

**MacOS / Linux:**
```bash
./bin/Rind
```

## Notes
- Shaders are automatically compiled from HLSL to SPIR-V using `dxc` during the build process.
- Assets are expected to be in `src/assets`.