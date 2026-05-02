cmake_minimum_required(VERSION 3.10)

project(Rind)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(BUNDLE_WINDOWS_RUNTIMES "Bundle required runtime DLLs next to Windows executable and in install package" ON)
option(BUNDLE_MACOS_RUNTIMES "Bundle required runtime dylibs and Vulkan ICD next to macOS executable" ON)
option(BUNDLE_LINUX_RUNTIMES "Bundle runtime .so files next to Linux executable for standalone distribution" ON)

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
  # Try to auto-detect a locally installed LunarG Vulkan SDK on macOS
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
  link_directories(${CMAKE_SOURCE_DIR}/include)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Windows builds are x64-only. Please configure with a 64-bit toolchain/generator.")
  endif()
endif()

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
endif()

set(SOURCES
    src/main.cpp
)

file(GLOB_RECURSE ENGINE_SOURCES "src/engine/*.cpp")
file(GLOB_RECURSE GAME_SOURCES "src/game/*.cpp")
file(GLOB_RECURSE RIND_SOURCES "src/rind/*.cpp")

list(APPEND SOURCES ${ENGINE_SOURCES} ${GAME_SOURCES} ${RIND_SOURCES})

add_executable(${PROJECT_NAME} ${SOURCES})

if(IS_MACOS)
  target_include_directories(${PROJECT_NAME} PRIVATE
        /usr/local/include
        /opt/homebrew/include
    )
  target_link_directories(${PROJECT_NAME} PRIVATE
        /usr/local/lib
        /opt/homebrew/lib
    )
  if(NOT VK_SDK_PATH)
    find_package(Vulkan REQUIRED)
    include_directories(${Vulkan_INCLUDE_DIRS})
  else()
    target_include_directories(${PROJECT_NAME} PRIVATE
      "${VK_SDK_PATH}/macOS/include"
    )
    target_link_directories(${PROJECT_NAME} PRIVATE
      "${VK_SDK_PATH}/macOS/lib"
    )
  endif()
elseif(IS_WINDOWS)
  if(VK_SDK_PATH)
    include_directories(${VK_SDK_PATH}/Include)
    link_directories(${VK_SDK_PATH}/Lib)
  elseif(DEFINED ENV{VULKAN_SDK})
    include_directories($ENV{VULKAN_SDK}/Include)
    link_directories($ENV{VULKAN_SDK}/Lib)
  else()
    find_package(Vulkan REQUIRED)
    include_directories(${Vulkan_INCLUDE_DIRS})
  endif()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/external
    ${CMAKE_SOURCE_DIR}/include/external/glm
    ${CMAKE_SOURCE_DIR}/include/external/glfw/include
    ${CMAKE_SOURCE_DIR}/include/external/fastgltf/include
    ${CMAKE_SOURCE_DIR}/include/external/freetype/include
)

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
else() # Linux
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

file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/src/assets/shaders/compiled)

file(GLOB_RECURSE HLSL_SOURCE_FILES
    "${CMAKE_SOURCE_DIR}/src/assets/shaders/hlsl/*.hlsl"
)

set(SPIRV_BINARY_FILES)

# Compile HLSL shaders to SPIR-V using dxc
foreach(HLSL ${HLSL_SOURCE_FILES})
  get_filename_component(FILE_NAME ${HLSL} NAME)
  set(SHADER_EXTRA_FLAGS)
  if(FILE_NAME MATCHES "\\.vert\\.hlsl$")
    set(SHADER_PROFILE "vs_6_0")
    if(FILE_NAME STREQUAL "shadow.vert.hlsl")
      set(SHADER_PROFILE "vs_6_1")
    endif()
  elseif(FILE_NAME MATCHES "\\.frag\\.hlsl$")
    set(SHADER_PROFILE "ps_6_0")
    if(FILE_NAME STREQUAL "volumetric.frag.hlsl")
      set(SHADER_PROFILE "ps_6_2")
      list(APPEND SHADER_EXTRA_FLAGS -HV 2021 -enable-16bit-types)
    endif()
  elseif(FILE_NAME MATCHES "\\.comp\\.hlsl$")
    set(SHADER_PROFILE "cs_6_2")
    list(APPEND SHADER_EXTRA_FLAGS -HV 2021 -enable-16bit-types)
  else()
    message(WARNING "Could not determine shader stage for ${FILE_NAME}")
    continue()
  endif()

  string(REPLACE ".hlsl" ".spv" SPIRV_NAME ${FILE_NAME})
  set(SPIRV "${CMAKE_SOURCE_DIR}/src/assets/shaders/compiled/${SPIRV_NAME}")

  add_custom_command(
        OUTPUT ${SPIRV}
      COMMAND ${DXC_EXECUTABLE} -spirv -O3 -fvk-use-scalar-layout -fspv-target-env=vulkan1.3 -fvk-use-dx-position-w ${SHADER_EXTRA_FLAGS} -T ${SHADER_PROFILE} -E main ${HLSL} -Fo ${SPIRV}
        DEPENDS ${HLSL}
        COMMENT "Compiling ${FILE_NAME} to SPIR-V"
    )
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(HLSL)

add_custom_target(
    Shaders
    DEPENDS ${SPIRV_BINARY_FILES}
    COMMENT "Compiling shaders"
)
add_dependencies(${PROJECT_NAME} Shaders)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GENERATED_ASSETS_DIR "${CMAKE_BINARY_DIR}/generated/assets")
file(MAKE_DIRECTORY ${GENERATED_ASSETS_DIR})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${GENERATED_ASSETS_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

set(ALL_EMBEDDED_SOURCES "")
set(ALL_EMBEDDED_OUTPUTS "")

# Embed assets into C++ source files
function(embed_asset_category)
    cmake_parse_arguments(EA "" "CATEGORY;DIRECTORY;RECURSIVE" "EXTENSIONS" ${ARGN})

    set(ASSET_FILES "")
    foreach(EXT ${EA_EXTENSIONS})
        if(EA_RECURSIVE)
            file(GLOB_RECURSE FOUND "${EA_DIRECTORY}/${EXT}")
        else()
            file(GLOB FOUND "${EA_DIRECTORY}/${EXT}")
        endif()
        list(APPEND ASSET_FILES ${FOUND})
    endforeach()

    set(CATEGORY_DIR "${GENERATED_ASSETS_DIR}/${EA_CATEGORY}")
    file(MAKE_DIRECTORY ${CATEGORY_DIR})
    set(CATEGORY_NAMES "")
    set(CATEGORY_SOURCES "")

    foreach(ASSET_FILE ${ASSET_FILES})
        file(RELATIVE_PATH REL_PATH "${EA_DIRECTORY}" "${ASSET_FILE}")
        get_filename_component(REL_DIR "${REL_PATH}" DIRECTORY)
        get_filename_component(FILE_NAME "${REL_PATH}" NAME)
        string(REGEX REPLACE "\\.[^.]+$" "" STEM "${FILE_NAME}")

        if(REL_DIR)
            string(REPLACE "/" "_" DIR_PREFIX "${REL_DIR}")
            set(ASSET_NAME "${DIR_PREFIX}_${STEM}")
        else()
            set(ASSET_NAME "${STEM}")
        endif()

        string(REGEX REPLACE "[^a-zA-Z0-9]" "_" SAFE_NAME "${ASSET_NAME}")
        set(OUT_CPP "${CATEGORY_DIR}/${EA_CATEGORY}_${SAFE_NAME}.cpp")
        set(OUT_H "${CATEGORY_DIR}/${EA_CATEGORY}_${SAFE_NAME}.h")

        add_custom_command(
            OUTPUT ${OUT_CPP} ${OUT_H}
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/cmake/embed_asset.py
                ${ASSET_FILE}
                ${CATEGORY_DIR}
                ${ASSET_NAME}
                ${EA_CATEGORY}
            DEPENDS ${ASSET_FILE} ${CMAKE_SOURCE_DIR}/cmake/embed_asset.py
            COMMENT "Embedding ${EA_CATEGORY}: ${ASSET_NAME}"
            VERBATIM
        )

        list(APPEND CATEGORY_NAMES "${ASSET_NAME}")
        list(APPEND CATEGORY_SOURCES ${OUT_CPP})
    endforeach()

    string(REPLACE ";" "\\;" NAMES_ESCAPED "${CATEGORY_NAMES}")
    set(REGISTRY_H "${CATEGORY_DIR}/${EA_CATEGORY}_registry.h")

    set(CATEGORY_HEADERS "")
    foreach(NAME ${CATEGORY_NAMES})
        string(REGEX REPLACE "[^a-zA-Z0-9]" "_" SAFE "${NAME}")
        list(APPEND CATEGORY_HEADERS "${CATEGORY_DIR}/${EA_CATEGORY}_${SAFE}.h")
    endforeach()

    add_custom_command(
        OUTPUT ${REGISTRY_H}
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/cmake/generate_registry.py
            ${EA_CATEGORY}
            ${CATEGORY_DIR}
            ${CATEGORY_NAMES}
        DEPENDS ${CATEGORY_HEADERS} ${CMAKE_SOURCE_DIR}/cmake/generate_registry.py
        COMMENT "Generating ${EA_CATEGORY} registry"
        VERBATIM
    )

    set(ALL_EMBEDDED_SOURCES ${ALL_EMBEDDED_SOURCES} ${CATEGORY_SOURCES} PARENT_SCOPE)
    set(ALL_EMBEDDED_OUTPUTS ${ALL_EMBEDDED_OUTPUTS} ${REGISTRY_H} ${CATEGORY_SOURCES} PARENT_SCOPE)
endfunction()

embed_asset_category(
    CATEGORY shader
    DIRECTORY "${CMAKE_SOURCE_DIR}/src/assets/shaders/compiled"
    EXTENSIONS "*.spv"
    RECURSIVE OFF
)
embed_asset_category(
    CATEGORY font
    DIRECTORY "${CMAKE_SOURCE_DIR}/src/assets/fonts"
    EXTENSIONS "*.ttf" "*.otf"
    RECURSIVE ON
)
embed_asset_category(
    CATEGORY audio
    DIRECTORY "${CMAKE_SOURCE_DIR}/src/assets/audio"
    EXTENSIONS "*.wav"
    RECURSIVE OFF
)
embed_asset_category(
    CATEGORY model
    DIRECTORY "${CMAKE_SOURCE_DIR}/src/assets/models"
    EXTENSIONS "*.glb"
    RECURSIVE ON
)
embed_asset_category(
    CATEGORY texture
    DIRECTORY "${CMAKE_SOURCE_DIR}/src/assets/textures"
    EXTENSIONS "*.png" "*.jpg" "*.hdr"
    RECURSIVE ON
)

target_sources(${PROJECT_NAME} PRIVATE ${ALL_EMBEDDED_SOURCES})
add_custom_target(EmbeddedAssets DEPENDS ${ALL_EMBEDDED_OUTPUTS})
add_dependencies(EmbeddedAssets Shaders)
add_dependencies(${PROJECT_NAME} EmbeddedAssets)

if(IS_MACOS)
  find_package(Vulkan REQUIRED)
  find_package(glfw3 REQUIRED)
  find_package(Freetype REQUIRED)

  target_link_libraries(${PROJECT_NAME}
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
  target_include_directories(${PROJECT_NAME} PRIVATE
        ${Vulkan_INCLUDE_DIRS}
        ${FREETYPE_INCLUDE_DIRS}
    )
  target_compile_definitions(${PROJECT_NAME} PRIVATE
        VULKAN_HPP_NO_EXCEPTIONS
        VK_USE_PLATFORM_MACOS_MVK
    )
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
  # Freetype
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
  add_subdirectory(${CMAKE_SOURCE_DIR}/include/external/glfw)

  target_link_libraries(${PROJECT_NAME} ${Vulkan_LIBRARY} glfw freetype fastgltf::fastgltf)
  target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32
    )
else() # Linux
  find_package(PkgConfig QUIET)
  find_package(Vulkan REQUIRED)

  if(BUNDLE_LINUX_RUNTIMES)
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
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_SOURCE_DIR}/include/external/glfw)

    target_link_libraries(${PROJECT_NAME}
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

    target_link_libraries(${PROJECT_NAME}
          Vulkan::Vulkan
          glfw
          ${FREETYPE_LIBRARIES}
          fastgltf::fastgltf
      )
    target_include_directories(${PROJECT_NAME} PRIVATE
      ${FREETYPE_INCLUDE_DIRS}
      $<IF:$<BOOL:${glfw3_FOUND}>,${glfw3_INCLUDE_DIRS},${GLFW3_INCLUDE_DIRS}>
      )
  endif()

  target_compile_definitions(${PROJECT_NAME} PRIVATE
        VULKAN_HPP_NO_EXCEPTIONS
    )

  set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "$ORIGIN;$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN;$ORIGIN/lib"
    )
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_OPENMP "Enable OpenMP parallelism" ON)
if(ENABLE_OPENMP)
  if(APPLE)
    find_package(OpenMP QUIET)
    if(OpenMP_CXX_FOUND)
      target_link_libraries(${PROJECT_NAME} OpenMP::OpenMP_CXX)
      target_compile_definitions(${PROJECT_NAME} PRIVATE USE_OPENMP)
      message(STATUS "OpenMP found via CMake and linked")
    else()
      set(_LIBOMP_PREFIX_CANDIDATES
                "/opt/homebrew/opt/libomp"
                "/usr/local/opt/libomp"
            )
      set(_LIBOMP_FOUND FALSE)
      foreach(_PFX IN LISTS _LIBOMP_PREFIX_CANDIDATES)
        if(EXISTS "${_PFX}/lib/libomp.dylib")
          target_include_directories(${PROJECT_NAME} PRIVATE "${_PFX}/include")
          target_compile_options(${PROJECT_NAME} PRIVATE -Xpreprocessor -fopenmp)
          target_link_libraries(${PROJECT_NAME} "${_PFX}/lib/libomp.dylib")
          target_compile_definitions(${PROJECT_NAME} PRIVATE USE_OPENMP)
          message(STATUS "Using Homebrew OpenMP at ${_PFX}")
          set(_LIBOMP_FOUND TRUE)
          break()
        endif()
      endforeach()
      if(NOT _LIBOMP_FOUND)
        message(WARNING "ENABLE_OPENMP=ON but libomp not found. Install with: brew install libomp. Proceeding without USE_OPENMP.")
      endif()
    endif()
  else()
    if(MSVC)
      set(OpenMP_MSVC_DEFAULT "LLVM" CACHE STRING "" FORCE)
    endif()

    find_package(OpenMP QUIET)
    if(OpenMP_CXX_FOUND)
      target_link_libraries(${PROJECT_NAME} OpenMP::OpenMP_CXX)
      target_compile_definitions(${PROJECT_NAME} PRIVATE USE_OPENMP)
      if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /openmp:llvm)
        message(STATUS "OpenMP found; enabling /openmp:llvm for MSVC")
      else()
        message(STATUS "OpenMP found and linked")
      endif()
    else()
      message(WARNING "ENABLE_OPENMP=ON but OpenMP not found; proceeding without USE_OPENMP.")
    endif()
  endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:-O3 -DNDEBUG -funroll-loops -fvectorize -fslp-vectorize -ffast-math -march=native -mtune=native -flto -fomit-frame-pointer -finline-functions -Wno-nan-infinity-disabled>
        $<$<CONFIG:Debug>:-Wall -Wextra -g -O0>
    )
  set_target_properties(${PROJECT_NAME} PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:-O3 -DNDEBUG -funroll-loops -ffast-math -march=native -mtune=native -flto -fomit-frame-pointer -finline-functions -ftree-vectorize>
        $<$<CONFIG:Debug>:-Wall -Wextra -g -O0>
    )
  set_target_properties(${PROJECT_NAME} PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  target_compile_options(${PROJECT_NAME} PRIVATE
        /FS
        $<$<CONFIG:Release>:/O2 /Oi /Ot /Gy /GL /fp:fast /DNDEBUG>
        $<$<CONFIG:Debug>:/W4 /Zi /Od>
    )
  set_target_properties(${PROJECT_NAME} PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )
endif()

if(MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE /FS)
endif()

# Bundle Windows runtime DLLs
if(IS_WINDOWS)
  install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION . CONFIGURATIONS Release)

  if(BUNDLE_WINDOWS_RUNTIMES)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
      install(FILES $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> DESTINATION . CONFIGURATIONS Release)
    endif()

    if(MSVC OR CMAKE_VERSION VERSION_LESS "3.21")
      set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
      set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
      set(CMAKE_INSTALL_OPENMP_LIBRARIES TRUE)
      include(InstallRequiredSystemLibraries)
      if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION . CONFIGURATIONS Release)
      endif()
    endif()

    if(MSVC)
      get_filename_component(_compiler_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
      foreach(_omp_dll_name libomp140.x86_64.dll libomp.dll vcomp140.dll)
        unset(_omp_dll_path CACHE)
        unset(_omp_dll_path)
        find_file(_omp_dll_path NAMES ${_omp_dll_name}
          HINTS
            "${_compiler_bin_dir}"
            "${_compiler_bin_dir}/../../../../../VC/Redist/MSVC"
            "${_compiler_bin_dir}/../../../../../VC/Tools/Llvm/x64/bin"
          PATH_SUFFIXES
            x64/Microsoft.VC143.CRT
            x64/Microsoft.VC142.CRT
        )
        if(_omp_dll_path)
          install(FILES "${_omp_dll_path}" DESTINATION . CONFIGURATIONS Release)
          if(CMAKE_CONFIGURATION_TYPES)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
              COMMAND ${CMAKE_COMMAND}
                -DBUILD_CONFIG="$<CONFIG>"
                -DSOURCE_FILE="${_omp_dll_path}"
                -DDEST_DIR="$<TARGET_FILE_DIR:${PROJECT_NAME}>"
                -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_release.cmake"
              COMMENT "Copying ${_omp_dll_name} to runtime output (Release only)"
            )
          elseif(_is_single_config_release)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
              COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_omp_dll_path}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
              COMMENT "Copying ${_omp_dll_name} to runtime output"
            )
          endif()
        endif()
      endforeach()
    elseif(MINGW)
      get_filename_component(_compiler_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
      foreach(_mingw_runtime_dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll libgomp-1.dll)
        unset(_mingw_runtime_path CACHE)
        unset(_mingw_runtime_path)
        find_file(_mingw_runtime_path NAMES ${_mingw_runtime_dll}
          HINTS
            "${_compiler_bin_dir}"
            "${_compiler_bin_dir}/../bin"
        )
        if(_mingw_runtime_path)
          install(FILES "${_mingw_runtime_path}" DESTINATION . CONFIGURATIONS Release)
          if(CMAKE_CONFIGURATION_TYPES)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
              COMMAND ${CMAKE_COMMAND}
                -DBUILD_CONFIG="$<CONFIG>"
                -DSOURCE_FILE="${_mingw_runtime_path}"
                -DDEST_DIR="$<TARGET_FILE_DIR:${PROJECT_NAME}>"
                -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_release.cmake"
              COMMENT "Copying ${_mingw_runtime_dll} to runtime output (Release only)"
            )
          elseif(_is_single_config_release)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
              COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_mingw_runtime_path}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
              COMMENT "Copying ${_mingw_runtime_dll} to runtime output"
            )
          endif()
        endif()
      endforeach()
    endif()
  endif()
endif()

# Bundle macOS runtime dylibs and Vulkan ICD
if(IS_MACOS)
  set_target_properties(${PROJECT_NAME} PROPERTIES
        INSTALL_RPATH "@executable_path;@executable_path/lib"
    )
  if(BUNDLE_MACOS_RUNTIMES AND (CMAKE_CONFIGURATION_TYPES OR _is_single_config_release))
    set(_macos_runtime_search_dirs)
    if(VK_SDK_PATH)
      list(APPEND _macos_runtime_search_dirs "${VK_SDK_PATH}/macOS/lib")
    endif()
    if(DEFINED ENV{VULKAN_SDK})
      list(APPEND _macos_runtime_search_dirs "$ENV{VULKAN_SDK}/macOS/lib")
    endif()
    list(APPEND _macos_runtime_search_dirs
          "/opt/homebrew/lib"
          "/opt/homebrew/opt/glfw/lib"
          "/opt/homebrew/opt/freetype/lib"
          "/opt/homebrew/opt/libomp/lib"
          "/opt/homebrew/opt/vulkan-loader/lib"
          "/opt/homebrew/opt/molten-vk/lib"
          "/usr/local/lib"
          "/usr/local/opt/glfw/lib"
          "/usr/local/opt/freetype/lib"
          "/usr/local/opt/libomp/lib"
          "/usr/local/opt/vulkan-loader/lib"
          "/usr/local/opt/molten-vk/lib"
      )

    set(_macos_existing_search_dirs)
    foreach(_dir IN LISTS _macos_runtime_search_dirs)
      if(IS_DIRECTORY "${_dir}")
        list(APPEND _macos_existing_search_dirs "${_dir}")
      endif()
    endforeach()
    list(REMOVE_DUPLICATES _macos_existing_search_dirs)

    set(_macos_extra_runtime_libs)

    set(_vulkan_loader_candidates)
    if(VK_SDK_PATH)
      list(APPEND _vulkan_loader_candidates "${VK_SDK_PATH}/macOS/lib/libvulkan.1.dylib")
    endif()
    if(DEFINED ENV{VULKAN_SDK})
      list(APPEND _vulkan_loader_candidates "$ENV{VULKAN_SDK}/macOS/lib/libvulkan.1.dylib")
    endif()
    list(APPEND _vulkan_loader_candidates
          "/opt/homebrew/opt/vulkan-loader/lib/libvulkan.1.dylib"
          "/usr/local/opt/vulkan-loader/lib/libvulkan.1.dylib"
      )
    set(_vulkan_loader_to_copy "")
    foreach(_cand IN LISTS _vulkan_loader_candidates)
      if(EXISTS "${_cand}")
        set(_vulkan_loader_to_copy "${_cand}")
        list(APPEND _macos_extra_runtime_libs "${_cand}")
        break()
      endif()
    endforeach()
    if(NOT _vulkan_loader_to_copy)
      message(WARNING "BUNDLE_MACOS_RUNTIMES=ON but libvulkan.1.dylib was not found. Set VK_SDK_PATH/VULKAN_SDK or install vulkan-loader.")
    endif()

    set(_moltenvk_candidates)
    if(VK_SDK_PATH)
      list(APPEND _moltenvk_candidates "${VK_SDK_PATH}/macOS/lib/libMoltenVK.dylib")
    endif()
    if(DEFINED ENV{VULKAN_SDK})
      list(APPEND _moltenvk_candidates "$ENV{VULKAN_SDK}/macOS/lib/libMoltenVK.dylib")
    endif()
    list(APPEND _moltenvk_candidates
          "/opt/homebrew/opt/molten-vk/lib/libMoltenVK.dylib"
          "/usr/local/opt/molten-vk/lib/libMoltenVK.dylib"
      )
    set(_moltenvk_to_copy "")
    foreach(_cand IN LISTS _moltenvk_candidates)
      if(EXISTS "${_cand}")
        set(_moltenvk_to_copy "${_cand}")
        list(APPEND _macos_extra_runtime_libs "${_cand}")
        break()
      endif()
    endforeach()
    if(NOT _moltenvk_to_copy)
      message(WARNING "BUNDLE_MACOS_RUNTIMES=ON but libMoltenVK.dylib was not found. Set VK_SDK_PATH/VULKAN_SDK or install molten-vk.")
    endif()

    set(_moltenvk_icd_candidates)
    if(VK_SDK_PATH)
      list(APPEND _moltenvk_icd_candidates "${VK_SDK_PATH}/macOS/share/vulkan/icd.d/MoltenVK_icd.json")
    endif()
    if(DEFINED ENV{VULKAN_SDK})
      list(APPEND _moltenvk_icd_candidates "$ENV{VULKAN_SDK}/share/vulkan/icd.d/MoltenVK_icd.json")
      list(APPEND _moltenvk_icd_candidates "$ENV{VULKAN_SDK}/macOS/share/vulkan/icd.d/MoltenVK_icd.json")
    endif()
    list(APPEND _moltenvk_icd_candidates
          "/opt/homebrew/share/vulkan/icd.d/MoltenVK_icd.json"
          "/usr/local/share/vulkan/icd.d/MoltenVK_icd.json"
      )
    set(_moltenvk_icd_source "")
    foreach(_cand IN LISTS _moltenvk_icd_candidates)
      if(EXISTS "${_cand}")
        set(_moltenvk_icd_source "${_cand}")
        break()
      endif()
    endforeach()

    set(_moltenvk_api_version "1.2.0")
    if(_moltenvk_icd_source)
      file(READ "${_moltenvk_icd_source}" _moltenvk_icd_contents)
      string(REGEX MATCH "\"api_version\"[ \t]*:[ \t]*\"([^\"]+)\"" _api_match "${_moltenvk_icd_contents}")
      if(CMAKE_MATCH_1)
        set(_moltenvk_api_version "${CMAKE_MATCH_1}")
      endif()
    endif()

    set(_moltenvk_icd_generated "${CMAKE_BINARY_DIR}/generated/MoltenVK_icd.json")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
    file(WRITE "${_moltenvk_icd_generated}" "{\n    \"file_format_version\": \"1.0.0\",\n    \"ICD\": {\n        \"library_path\": \"../lib/libMoltenVK.dylib\",\n        \"api_version\": \"${_moltenvk_api_version}\",\n        \"is_portability_driver\": true\n    }\n}\n")

    set(_macos_bundle_script "${CMAKE_BINARY_DIR}/cmake/macos_bundle_runtime.cmake")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/cmake")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/macos_bundle_runtime.cmake.in" "${_macos_bundle_script}" @ONLY)

    string(REPLACE ";" "|" _macos_search_dirs_pipe "${_macos_existing_search_dirs}")
    string(REPLACE ";" "|" _macos_extra_libs_pipe "${_macos_extra_runtime_libs}")

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
          -DBUILD_CONFIG="$<CONFIG>"
                -DAPP_PATH="$<TARGET_FILE:${PROJECT_NAME}>"
                -DAPP_DIR="$<TARGET_FILE_DIR:${PROJECT_NAME}>"
                -DSEARCH_DIRS_PIPE="${_macos_search_dirs_pipe}"
                -DEXTRA_LIBS_PIPE="${_macos_extra_libs_pipe}"
                -DMOLTENVK_ICD_TEMPLATE="${_moltenvk_icd_generated}"
                -P "${_macos_bundle_script}"
            COMMENT "Bundling macOS runtime dylibs and Vulkan ICD"
        )
  endif()
endif()

# Bundle Linux runtime shared libraries
if(IS_LINUX)
  if(BUNDLE_LINUX_RUNTIMES AND (CMAKE_CONFIGURATION_TYPES OR _is_single_config_release))
    set(_linux_bundle_script "${CMAKE_BINARY_DIR}/cmake/linux_bundle_runtime.cmake")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/cmake")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/linux_bundle_runtime.cmake.in" "${_linux_bundle_script}" @ONLY)

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -DBUILD_CONFIG="$<CONFIG>"
                -DAPP_PATH="$<TARGET_FILE:${PROJECT_NAME}>"
                -DAPP_DIR="$<TARGET_FILE_DIR:${PROJECT_NAME}>"
                -P "${_linux_bundle_script}"
            COMMENT "Bundling Linux runtime shared libraries"
        )
  endif()
endif()
