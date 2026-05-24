# Helpers exported by the rind_engine target for downstream consumers
#
#   embed_asset_category(TARGET <tgt> CATEGORY <name> DIRECTORY <dir>
#                        EXTENSIONS <ext...> [RECURSIVE ON|OFF])
#       Globs assets, runs embed_asset.py + generate_registry.py, and attaches
#       the generated .cpp files to <tgt>. Adds the generated registry header
#       directory to <tgt>'s include path
#
#   rind_engine_compile_shaders(TARGET <tgt> SOURCE_DIR <dir> OUT_DIR <dir>
#                               OUTPUT_LIST <var>)
#       Compiles every *.hlsl file under <SOURCE_DIR> to SPIR-V using dxc and
#       writes the .spv files to <OUT_DIR>. Stores the list of produced .spv
#       paths in the parent-scope variable <OUTPUT_LIST>
#
#   rind_engine_bundle_runtimes(<target_name>)
#       Adds platform-appropriate post-build / install rules to bundle the
#       runtime dependencies of <target_name>

if(DEFINED _RIND_ENGINE_CMAKE_INCLUDED)
    return()
endif()
set(_RIND_ENGINE_CMAKE_INCLUDED TRUE)

set(RIND_ENGINE_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(rind_engine_compile_shaders)
    cmake_parse_arguments(RCS "" "TARGET;SOURCE_DIR;OUT_DIR;OUTPUT_LIST" "" ${ARGN})

    if(NOT DXC_EXECUTABLE)
        message(FATAL_ERROR "rind_engine_compile_shaders: DXC_EXECUTABLE not set. Locate dxc before calling.")
    endif()

    file(MAKE_DIRECTORY "${RCS_OUT_DIR}")
    file(GLOB_RECURSE _HLSL_FILES "${RCS_SOURCE_DIR}/*.hlsl")

    set(_SPIRV_FILES)
    foreach(HLSL ${_HLSL_FILES})
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
        set(SPIRV "${RCS_OUT_DIR}/${SPIRV_NAME}")

        add_custom_command(
            OUTPUT ${SPIRV}
            COMMAND ${DXC_EXECUTABLE} -spirv -O3 -fvk-use-scalar-layout -fspv-target-env=vulkan1.3 -fvk-use-dx-position-w ${SHADER_EXTRA_FLAGS} -T ${SHADER_PROFILE} -E main ${HLSL} -Fo ${SPIRV}
            DEPENDS ${HLSL}
            COMMENT "Compiling ${FILE_NAME} to SPIR-V"
        )
        list(APPEND _SPIRV_FILES ${SPIRV})
    endforeach()

    if(RCS_TARGET)
        add_custom_target(${RCS_TARGET}_Shaders DEPENDS ${_SPIRV_FILES} COMMENT "Compiling shaders for ${RCS_TARGET}")
        add_dependencies(${RCS_TARGET} ${RCS_TARGET}_Shaders)
    endif()

    if(RCS_OUTPUT_LIST)
        set(${RCS_OUTPUT_LIST} ${_SPIRV_FILES} PARENT_SCOPE)
    endif()
endfunction()

function(embed_asset_category)
    cmake_parse_arguments(EA "" "TARGET;CATEGORY;DIRECTORY;RECURSIVE" "EXTENSIONS" ${ARGN})

    if(NOT EA_TARGET)
        message(FATAL_ERROR "embed_asset_category: TARGET argument is required.")
    endif()
    if(NOT EA_CATEGORY)
        message(FATAL_ERROR "embed_asset_category: CATEGORY argument is required.")
    endif()
    if(NOT EA_DIRECTORY)
        message(FATAL_ERROR "embed_asset_category: DIRECTORY argument is required.")
    endif()

    set(GENERATED_ROOT "${CMAKE_BINARY_DIR}/generated/assets/${EA_TARGET}")
    set(CATEGORY_DIR "${GENERATED_ROOT}/${EA_CATEGORY}")
    file(MAKE_DIRECTORY ${CATEGORY_DIR})

    set(ASSET_FILES "")
    foreach(EXT ${EA_EXTENSIONS})
        if(EA_RECURSIVE)
            file(GLOB_RECURSE FOUND "${EA_DIRECTORY}/${EXT}")
        else()
            file(GLOB FOUND "${EA_DIRECTORY}/${EXT}")
        endif()
        list(APPEND ASSET_FILES ${FOUND})
    endforeach()

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
                ${RIND_ENGINE_CMAKE_DIR}/embed_asset.py
                ${ASSET_FILE}
                ${CATEGORY_DIR}
                ${ASSET_NAME}
                ${EA_CATEGORY}
            DEPENDS ${ASSET_FILE} ${RIND_ENGINE_CMAKE_DIR}/embed_asset.py
            COMMENT "Embedding ${EA_CATEGORY}: ${ASSET_NAME}"
            VERBATIM
        )

        list(APPEND CATEGORY_NAMES "${ASSET_NAME}")
        list(APPEND CATEGORY_SOURCES ${OUT_CPP})
    endforeach()

    set(REGISTRY_H "${CATEGORY_DIR}/${EA_CATEGORY}_registry.h")
    set(CATEGORY_HEADERS "")
    foreach(NAME ${CATEGORY_NAMES})
        string(REGEX REPLACE "[^a-zA-Z0-9]" "_" SAFE "${NAME}")
        list(APPEND CATEGORY_HEADERS "${CATEGORY_DIR}/${EA_CATEGORY}_${SAFE}.h")
    endforeach()

    add_custom_command(
        OUTPUT ${REGISTRY_H}
        COMMAND ${Python3_EXECUTABLE}
            ${RIND_ENGINE_CMAKE_DIR}/generate_registry.py
            ${EA_CATEGORY}
            ${CATEGORY_DIR}
            ${CATEGORY_NAMES}
        DEPENDS ${CATEGORY_HEADERS} ${RIND_ENGINE_CMAKE_DIR}/generate_registry.py
        COMMENT "Generating ${EA_CATEGORY} registry for ${EA_TARGET}"
        VERBATIM
    )

    target_sources(${EA_TARGET} PRIVATE ${CATEGORY_SOURCES} ${REGISTRY_H})
    target_include_directories(${EA_TARGET} PRIVATE ${GENERATED_ROOT})

    set(_assets_tgt "${EA_TARGET}_${EA_CATEGORY}_Embed")
    add_custom_target(${_assets_tgt} DEPENDS ${REGISTRY_H} ${CATEGORY_SOURCES})
    add_dependencies(${EA_TARGET} ${_assets_tgt})
    if(TARGET ${EA_TARGET}_Shaders)
        add_dependencies(${_assets_tgt} ${EA_TARGET}_Shaders)
    endif()
endfunction()

function(rind_engine_bundle_runtimes target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "rind_engine_bundle_runtimes: target '${target_name}' does not exist.")
    endif()

    set(_is_single_config_release FALSE)
    if(NOT CMAKE_CONFIGURATION_TYPES AND CMAKE_BUILD_TYPE STREQUAL "Release")
        set(_is_single_config_release TRUE)
    endif()

    if(WIN32)
        install(TARGETS ${target_name} RUNTIME DESTINATION . CONFIGURATIONS Release)

        if(BUNDLE_WINDOWS_RUNTIMES)
            if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
                install(FILES $<TARGET_RUNTIME_DLLS:${target_name}> DESTINATION . CONFIGURATIONS Release)
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

            if(MINGW)
                get_filename_component(_compiler_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
                foreach(_mingw_runtime_dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
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
                            add_custom_command(TARGET ${target_name} POST_BUILD
                                COMMAND ${CMAKE_COMMAND}
                                    -DBUILD_CONFIG="$<CONFIG>"
                                    -DSOURCE_FILE="${_mingw_runtime_path}"
                                    -DDEST_DIR="$<TARGET_FILE_DIR:${target_name}>"
                                    -P "${RIND_ENGINE_CMAKE_DIR}/copy_if_release.cmake"
                                COMMENT "Copying ${_mingw_runtime_dll} to runtime output (Release only)"
                            )
                        elseif(_is_single_config_release)
                            add_custom_command(TARGET ${target_name} POST_BUILD
                                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_mingw_runtime_path}" "$<TARGET_FILE_DIR:${target_name}>"
                                COMMENT "Copying ${_mingw_runtime_dll} to runtime output"
                            )
                        endif()
                    endif()
                endforeach()
            endif()
        endif()
    elseif(APPLE)
        set_target_properties(${target_name} PROPERTIES
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
                "/opt/homebrew/opt/vulkan-loader/lib"
                "/opt/homebrew/opt/molten-vk/lib"
                "/usr/local/lib"
                "/usr/local/opt/glfw/lib"
                "/usr/local/opt/freetype/lib"
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

            set(_macos_bundle_script "${CMAKE_BINARY_DIR}/cmake/${target_name}_macos_bundle_runtime.cmake")
            file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/cmake")
            configure_file("${RIND_ENGINE_CMAKE_DIR}/macos_bundle_runtime.cmake.in" "${_macos_bundle_script}" @ONLY)

            string(REPLACE ";" "|" _macos_search_dirs_pipe "${_macos_existing_search_dirs}")
            string(REPLACE ";" "|" _macos_extra_libs_pipe "${_macos_extra_runtime_libs}")

            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND}
                    -DBUILD_CONFIG="$<CONFIG>"
                    -DAPP_PATH="$<TARGET_FILE:${target_name}>"
                    -DAPP_DIR="$<TARGET_FILE_DIR:${target_name}>"
                    -DSEARCH_DIRS_PIPE="${_macos_search_dirs_pipe}"
                    -DEXTRA_LIBS_PIPE="${_macos_extra_libs_pipe}"
                    -DMOLTENVK_ICD_TEMPLATE="${_moltenvk_icd_generated}"
                    -P "${_macos_bundle_script}"
                COMMENT "Bundling macOS runtime dylibs and Vulkan ICD"
            )
        endif()
    else()
        # Linux
        set_target_properties(${target_name} PROPERTIES
            BUILD_RPATH "$ORIGIN;$ORIGIN/lib"
            INSTALL_RPATH "$ORIGIN;$ORIGIN/lib"
        )
        if(BUNDLE_LINUX_RUNTIMES AND (CMAKE_CONFIGURATION_TYPES OR _is_single_config_release))
            set(_linux_bundle_script "${CMAKE_BINARY_DIR}/cmake/${target_name}_linux_bundle_runtime.cmake")
            file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/cmake")
            configure_file("${RIND_ENGINE_CMAKE_DIR}/linux_bundle_runtime.cmake.in" "${_linux_bundle_script}" @ONLY)

            set(_linux_extra_libs "")
            string(REPLACE ";" "|" _linux_extra_libs_pipe "${_linux_extra_libs}")

            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND}
                    -DBUILD_CONFIG="$<CONFIG>"
                    -DAPP_PATH="$<TARGET_FILE:${target_name}>"
                    -DAPP_DIR="$<TARGET_FILE_DIR:${target_name}>"
                    -DEXTRA_LIBS_PIPE="${_linux_extra_libs_pipe}"
                    -P "${_linux_bundle_script}"
                COMMENT "Bundling Linux runtime shared libraries"
            )
        endif()
    endif()
endfunction()
