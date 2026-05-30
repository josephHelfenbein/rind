cmake_minimum_required(VERSION 3.21)

project(Rind LANGUAGES C CXX ISPC)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ISPC settings: which CPU targets to emit per .ispc source
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
  set(CMAKE_ISPC_INSTRUCTION_SETS "neon-i32x4" CACHE STRING "ISPC target ISAs")
else()
  set(CMAKE_ISPC_INSTRUCTION_SETS "sse2-i32x4;sse4-i32x4;avx2-i32x8" CACHE STRING "ISPC target ISAs")
endif()

set(CMAKE_ISPC_HEADER_SUFFIX "_ispc.h")
option(BUNDLE_WINDOWS_RUNTIMES "Bundle required runtime DLLs next to Windows executable and in install package" ON)
option(BUNDLE_MACOS_RUNTIMES "Bundle required runtime dylibs and Vulkan ICD next to macOS executable" ON)
option(BUNDLE_LINUX_RUNTIMES "Bundle runtime .so files next to Linux executable for standalone distribution" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(RELEASE_MARCH "native" CACHE STRING "Target -march for release builds (e.g. native, x86-64-v2, znver2)")

set(_is_single_config_release FALSE)
if(NOT CMAKE_CONFIGURATION_TYPES AND CMAKE_BUILD_TYPE STREQUAL "Release")
  set(_is_single_config_release TRUE)
endif()

set(IS_WINDOWS FALSE)
set(IS_MACOS FALSE)
set(IS_LINUX FALSE)
if(WIN32)
  set(IS_WINDOWS TRUE)
elseif(APPLE)
  set(IS_MACOS TRUE)
else()
  set(IS_LINUX TRUE)
endif()

set(VK_SDK_PATH $ENV{VK_SDK_PATH})
if(NOT VK_SDK_PATH AND DEFINED ENV{VULKAN_SDK})
  set(VK_SDK_PATH $ENV{VULKAN_SDK})
endif()
if(APPLE AND NOT VK_SDK_PATH)
  set(_LUNARG_BASE "$ENV{HOME}/VulkanSDK")
  if(IS_DIRECTORY "${_LUNARG_BASE}")
    file(GLOB _LUNARG_SDK_CANDIDATES "${_LUNARG_BASE}/*")
    if(_LUNARG_SDK_CANDIDATES)
      list(SORT _LUNARG_SDK_CANDIDATES)
      list(REVERSE _LUNARG_SDK_CANDIDATES)
      foreach(_SDK_PATH IN LISTS _LUNARG_SDK_CANDIDATES)
        if(IS_DIRECTORY "${_SDK_PATH}/macOS/lib")
          set(VK_SDK_PATH ${_SDK_PATH})
          message(STATUS "Detected LunarG Vulkan SDK at ${VK_SDK_PATH}")
          break()
        endif()
      endforeach()
    endif()
  endif()
endif()
if(IS_WINDOWS AND NOT VK_SDK_PATH)
  message(STATUS "VK_SDK_PATH not set; will try find_package(Vulkan) on Windows")
endif()

if(IS_WINDOWS)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Windows builds are x64-only. Please configure with a 64-bit toolchain/generator.")
  endif()
endif()

# Third-party dependencies that the engine links against
set(FASTGLTF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_DOCS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_COMPILE_AS_CPP20 ON CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
if(IS_WINDOWS)
  set(CMAKE_SKIP_INSTALL_RULES ON)
endif()
add_subdirectory(${CMAKE_SOURCE_DIR}/include/external/fastgltf)
if(IS_WINDOWS)
  set(CMAKE_SKIP_INSTALL_RULES OFF)
endif()
if(TARGET fastgltf)
  target_compile_options(fastgltf PRIVATE -Wno-nan-infinity-disabled)
  if(NOT RELEASE_MARCH STREQUAL "native")
    target_compile_definitions(fastgltf PRIVATE
      SIMDJSON_IMPLEMENTATION_ICELAKE=0
      SIMDJSON_IMPLEMENTATION_HASWELL=0
    )
    target_compile_options(fastgltf PRIVATE -mcrc32)
  endif()
endif()

# Bundled freetype + glfw (Windows always, Linux when bundling, macOS uses system)
if(IS_WINDOWS OR (IS_LINUX AND BUNDLE_LINUX_RUNTIMES))
  set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
  set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
  add_subdirectory(${CMAKE_SOURCE_DIR}/include/external/freetype)

  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  if(IS_LINUX)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
  endif()
  add_subdirectory(${CMAKE_SOURCE_DIR}/include/external/glfw)
endif()

# Locate dxc once so the engine can compile its shaders
if(IS_MACOS)
  find_program(DXC_EXECUTABLE dxc
        HINTS
            ${VK_SDK_PATH}/macOS/bin
            /usr/local/bin
            $ENV{VULKAN_SDK}/bin
        PATHS
            /Users/$ENV{USER}/VulkanSDK/*/macOS/bin
    )
elseif(IS_WINDOWS)
  find_program(DXC_EXECUTABLE dxc
        HINTS
            ${VK_SDK_PATH}/Bin
            $ENV{VULKAN_SDK}/Bin
    )
else()
  find_program(DXC_EXECUTABLE dxc
        HINTS
            ${VK_SDK_PATH}/bin
            $ENV{VULKAN_SDK}/bin
            /usr/bin
            /usr/local/bin
    )
endif()
if(NOT DXC_EXECUTABLE)
  message(FATAL_ERROR "dxc not found! Please install Vulkan SDK.")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/RindEngine.cmake)

# rind_engine library
file(GLOB_RECURSE ENGINE_SOURCES "src/engine/*.cpp" "src/engine/*.ispc" "src/engine/*.mm")
add_library(rind_engine STATIC ${ENGINE_SOURCES})

# ISPC header output
target_include_directories(rind_engine PRIVATE
    "${CMAKE_BINARY_DIR}/CMakeFiles/rind_engine.dir")

target_include_directories(rind_engine PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/external
    ${CMAKE_SOURCE_DIR}/include/external/glm
    ${CMAKE_SOURCE_DIR}/include/external/glfw/include
    ${CMAKE_SOURCE_DIR}/include/external/fastgltf/include
    ${CMAKE_SOURCE_DIR}/include/external/freetype/include
    ${CMAKE_BINARY_DIR}/generated
)

if(IS_MACOS)
  target_include_directories(rind_engine PUBLIC
    /usr/local/include
    /opt/homebrew/include
  )
  target_link_directories(rind_engine PUBLIC
    /usr/local/lib
    /opt/homebrew/lib
  )
  if(VK_SDK_PATH)
    target_include_directories(rind_engine PUBLIC "${VK_SDK_PATH}/macOS/include")
    target_link_directories(rind_engine PUBLIC "${VK_SDK_PATH}/macOS/lib")
  endif()
elseif(IS_WINDOWS)
  if(VK_SDK_PATH)
    target_include_directories(rind_engine PUBLIC ${VK_SDK_PATH}/Include)
    target_link_directories(rind_engine PUBLIC ${VK_SDK_PATH}/Lib)
  elseif(DEFINED ENV{VULKAN_SDK})
    target_include_directories(rind_engine PUBLIC $ENV{VULKAN_SDK}/Include)
    target_link_directories(rind_engine PUBLIC $ENV{VULKAN_SDK}/Lib)
  endif()
endif()

# Platform link libs and defines (PUBLIC so consumer targets inherit)
if(IS_MACOS)
  find_package(Vulkan REQUIRED)
  find_package(glfw3 REQUIRED)
  find_package(Freetype REQUIRED)
  target_link_libraries(rind_engine PUBLIC
    ${Vulkan_LIBRARIES}
    glfw
    ${FREETYPE_LIBRARIES}
    fastgltf::fastgltf
    "-framework Cocoa"
    "-framework IOKit"
    "-framework CoreVideo"
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework AudioUnit"
  )
  target_include_directories(rind_engine PUBLIC
    ${Vulkan_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
  )
  target_compile_definitions(rind_engine PUBLIC
    VULKAN_HPP_NO_EXCEPTIONS
    VK_USE_PLATFORM_MACOS_MVK
  )
elseif(IS_WINDOWS)
  if(VK_SDK_PATH)
    find_library(Vulkan_LIBRARY vulkan-1 PATHS ${VK_SDK_PATH}/Lib NO_DEFAULT_PATH)
  endif()
  if(NOT Vulkan_LIBRARY AND DEFINED ENV{VULKAN_SDK})
    find_library(Vulkan_LIBRARY vulkan-1 PATHS $ENV{VULKAN_SDK}/Lib NO_DEFAULT_PATH)
  endif()
  if(NOT Vulkan_LIBRARY)
    find_package(Vulkan REQUIRED)
    set(Vulkan_LIBRARY Vulkan::Vulkan)
  endif()
  target_link_libraries(rind_engine PUBLIC ${Vulkan_LIBRARY} glfw freetype fastgltf::fastgltf)
  target_compile_definitions(rind_engine PUBLIC WIN32)
else()
  find_package(PkgConfig QUIET)
  find_package(Vulkan REQUIRED)

  if(BUNDLE_LINUX_RUNTIMES)
    target_link_libraries(rind_engine PUBLIC
      Vulkan::Vulkan
      glfw
      freetype
      fastgltf::fastgltf
    )
  else()
    find_package(Freetype REQUIRED)
    find_package(glfw3 3.3 QUIET)
    if(NOT glfw3_FOUND)
      if(PkgConfig_FOUND)
        pkg_check_modules(GLFW3 REQUIRED glfw3)
        add_library(glfw INTERFACE)
        target_include_directories(glfw INTERFACE ${GLFW3_INCLUDE_DIRS})
        target_link_libraries(glfw INTERFACE ${GLFW3_LIBRARIES})
      else()
        message(FATAL_ERROR "glfw3 not found. Please install it, e.g., sudo apt install libglfw3-dev")
      endif()
    endif()
    target_link_libraries(rind_engine PUBLIC
      Vulkan::Vulkan
      glfw
      ${FREETYPE_LIBRARIES}
      fastgltf::fastgltf
    )
    target_include_directories(rind_engine PUBLIC
      ${FREETYPE_INCLUDE_DIRS}
      $<IF:$<BOOL:${glfw3_FOUND}>,${glfw3_INCLUDE_DIRS},${GLFW3_INCLUDE_DIRS}>
    )
  endif()
  target_compile_definitions(rind_engine PUBLIC VULKAN_HPP_NO_EXCEPTIONS)
endif()

# Engine-owned shaders
rind_engine_compile_shaders(
    TARGET rind_engine
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/engine/assets/shaders/hlsl"
    OUT_DIR "${CMAKE_SOURCE_DIR}/src/engine/assets/shaders/compiled"
)

embed_asset_category(
    TARGET rind_engine
    CATEGORY shader
    DIRECTORY "${CMAKE_SOURCE_DIR}/src/engine/assets/shaders/compiled"
    EXTENSIONS "*.spv"
    RECURSIVE OFF
)


# Rind executable
file(GLOB_RECURSE RIND_SOURCES "src/rind/*.cpp")
set(RIND_EXTRA_SOURCES)
if(WIN32)
    list(APPEND RIND_EXTRA_SOURCES "${CMAKE_SOURCE_DIR}/cmake/Rind.rc")
endif()
add_executable(${PROJECT_NAME} src/main.cpp ${RIND_SOURCES} ${RIND_EXTRA_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src/rind/include)
target_link_libraries(${PROJECT_NAME} PRIVATE rind_engine)

# Game-owned assets
rind_engine_compile_shaders(
    TARGET ${PROJECT_NAME}
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/rind/assets/shaders/hlsl"
    OUT_DIR "${CMAKE_SOURCE_DIR}/src/rind/assets/shaders/compiled"
)
embed_asset_category(
    TARGET ${PROJECT_NAME}
    CATEGORY game_shader
    DIRECTORY "${CMAKE_SOURCE_DIR}/src/rind/assets/shaders/compiled"
    EXTENSIONS "*.spv"
    RECURSIVE OFF
)
embed_asset_category(
    TARGET ${PROJECT_NAME}
    CATEGORY font
    DIRECTORY "${CMAKE_SOURCE_DIR}/src/assets/fonts"
    EXTENSIONS "*.ttf" "*.otf"
    RECURSIVE ON
)
embed_asset_category(
    TARGET ${PROJECT_NAME}
    CATEGORY audio
    DIRECTORY "${CMAKE_SOURCE_DIR}/src/assets/audio"
    EXTENSIONS "*.wav"
    RECURSIVE OFF
)
embed_asset_category(
    TARGET ${PROJECT_NAME}
    CATEGORY model
    DIRECTORY "${CMAKE_SOURCE_DIR}/src/assets/models"
    EXTENSIONS "*.glb"
    RECURSIVE ON
)
embed_asset_category(
    TARGET ${PROJECT_NAME}
    CATEGORY texture
    DIRECTORY "${CMAKE_SOURCE_DIR}/src/assets/textures"
    EXTENSIONS "*.png" "*.jpg" "*.hdr"
    RECURSIVE ON
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

if(IS_MACOS)
  set(_vulkan_rpaths)
  if(VK_SDK_PATH)
    list(APPEND _vulkan_rpaths "${VK_SDK_PATH}/macOS/lib")
  elseif(DEFINED ENV{VULKAN_SDK})
    list(APPEND _vulkan_rpaths "$ENV{VULKAN_SDK}/macOS/lib")
  endif()
  list(APPEND _vulkan_rpaths
    "/opt/homebrew/lib"
    "/opt/homebrew/opt/vulkan-loader/lib"
    "/opt/homebrew/opt/vulkan-sdk/lib"
    "/opt/homebrew/opt/molten-vk/lib"
    "/usr/local/lib"
  )
  list(REMOVE_DUPLICATES _vulkan_rpaths)
  if(_vulkan_rpaths)
    set_target_properties(${PROJECT_NAME} PROPERTIES
      BUILD_RPATH "${_vulkan_rpaths}"
      INSTALL_RPATH "${_vulkan_rpaths}"
    )
  endif()
endif()

# Compiler flags: apply to both targets (ISPC has its own flag syntax)
foreach(_tgt rind_engine ${PROJECT_NAME})
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${_tgt} PRIVATE
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:-O3;-DNDEBUG;-funroll-loops;-fvectorize;-fslp-vectorize;-ffast-math;-march=${RELEASE_MARCH};-mtune=${RELEASE_MARCH};-flto;-fomit-frame-pointer;-finline-functions;-Wno-nan-infinity-disabled>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:-Wall;-Wextra;-g;-O0>"
    )
    set_target_properties(${_tgt} PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(${_tgt} PRIVATE
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:-O3;-DNDEBUG;-funroll-loops;-ffast-math;-march=${RELEASE_MARCH};-mtune=${RELEASE_MARCH};-flto;-fomit-frame-pointer;-finline-functions;-ftree-vectorize>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:-Wall;-Wextra;-g;-O0>"
    )
    set_target_properties(${_tgt} PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(${_tgt} PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/FS>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/O2;/Oi;/Ot;/Gy;/GL;/fp:fast;/DNDEBUG>"
      "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/W4;/Zi;/Od>"
    )
    set_target_properties(${_tgt} PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  endif()
  if(MSVC)
    target_compile_options(${_tgt} PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/FS>")
  endif()
endforeach()

# Runtime bundling for the executable
rind_engine_bundle_runtimes(${PROJECT_NAME})
