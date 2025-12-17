# Rind

## Build & Run (CMake)

Prereqs (all platforms)
- CMake 3.10+
- Vulkan SDK 1.3+ (dynamic rendering is used) with runtime loader available (`libvulkan.so.1` / `libvulkan.1.dylib` / `vulkan-1.dll`)
- `dxc` on PATH (comes with the Vulkan SDK)
- C++20 compiler

### Configure
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

### Build
```
cmake --build build --target Rind --config Release
```

### Run
```
./bin/Rind
```

Notes:
- Shaders are auto-built into `src/assets/shaders/compiled/` via the `Shaders` custom target; running `cmake --build` will invoke `dxc` for the HLSL files.