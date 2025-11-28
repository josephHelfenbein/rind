cmake_minimum_required(VERSION 3.10)

project(Rind)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
if(IS_WINDOWS AND NOT VK_SDK_PATH)
    message(STATUS "VK_SDK_PATH not set; will try find_package(Vulkan) on Windows")
endif()

if(IS_MACOS)
    include_directories(/usr/local/include)
    link_directories(/usr/local/lib)
    if(NOT VK_SDK_PATH)
        find_package(Vulkan REQUIRED)
        include_directories(${Vulkan_INCLUDE_DIRS})
    else()
        include_directories(${VK_SDK_PATH}/Include)
        link_directories(${VK_SDK_PATH}/macOS/lib)
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

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/include/glm)
include_directories(${CMAKE_SOURCE_DIR}/include/fastgltf/include)
if(IS_WINDOWS)
    link_directories(${CMAKE_SOURCE_DIR}/include)
endif()

set(FASTGLTF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_DOCS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_COMPILE_AS_CPP20 ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/include/fastgltf)

set(SOURCES
    src/main.cpp
)

file(GLOB_RECURSE ENGINE_SOURCES "src/engine/*.cpp")
file(GLOB_RECURSE GAME_SOURCES "src/game/*.cpp")

list(APPEND SOURCES ${ENGINE_SOURCES} ${GAME_SOURCES})

add_executable(${PROJECT_NAME} ${SOURCES})

if(IS_MACOS)
    find_program(GLSLC_EXECUTABLE glslc
        HINTS
            ${VK_SDK_PATH}/macOS/bin
            /usr/local/bin
            $ENV{VULKAN_SDK}/bin
        PATHS
            /Users/$ENV{USER}/VulkanSDK/*/macOS/bin
    )
elseif(IS_WINDOWS)
    find_program(GLSLC_EXECUTABLE glslc
        HINTS
            ${VK_SDK_PATH}/Bin
            $ENV{VULKAN_SDK}/Bin
    )
else() # Linux
    find_program(GLSLC_EXECUTABLE glslc
        HINTS
            ${VK_SDK_PATH}/bin
            $ENV{VULKAN_SDK}/bin
            /usr/bin
            /usr/local/bin
    )
endif()

if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found! Please install Vulkan SDK.")
endif()

file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/src/assets/shaders/compiled)

file(GLOB_RECURSE GLSL_SOURCE_FILES
    "${CMAKE_SOURCE_DIR}/src/assets/shaders/glsl/*.frag"
    "${CMAKE_SOURCE_DIR}/src/assets/shaders/glsl/*.vert"
    "${CMAKE_SOURCE_DIR}/src/assets/shaders/glsl/*.comp"
)

set(SPIRV_BINARY_FILES)

foreach(GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${CMAKE_SOURCE_DIR}/src/assets/shaders/compiled/${FILE_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${GLSLC_EXECUTABLE} ${GLSL} -o ${SPIRV}
        DEPENDS ${GLSL}
        COMMENT "Compiling ${FILE_NAME}"
    )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
    Shaders
    DEPENDS ${SPIRV_BINARY_FILES}
    COMMENT "Compiling shaders"
)

add_dependencies(${PROJECT_NAME} Shaders)

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
    )
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${Vulkan_INCLUDE_DIRS} 
        ${FREETYPE_INCLUDE_DIRS}
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        VULKAN_HPP_NO_EXCEPTIONS
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
    find_package(Freetype REQUIRED)

    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_SOURCE_DIR}/include/glfw)

    target_link_libraries(${PROJECT_NAME} ${Vulkan_LIBRARY} glfw ${FREETYPE_LIBRARIES} fastgltf::fastgltf)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${FREETYPE_INCLUDE_DIRS}
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32
    )
else() # Linux
    find_package(PkgConfig QUIET)
    find_package(Vulkan REQUIRED)
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
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        VULKAN_HPP_NO_EXCEPTIONS
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
            if(MSVC)
                target_compile_options(${PROJECT_NAME} PRIVATE /openmp:llvm)
                target_compile_definitions(${PROJECT_NAME} PRIVATE USE_OPENMP)
                message(WARNING "ENABLE_OPENMP=ON but OpenMP not found; forcing /openmp:llvm for MSVC. Ensure LLVM OpenMP runtime is installed.")
            else()
                message(WARNING "ENABLE_OPENMP=ON but OpenMP not found; proceeding without USE_OPENMP.")
            endif()
        endif()
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:-O3 -DNDEBUG -funroll-loops -fvectorize -fslp-vectorize -ffast-math -march=native -mtune=native -flto -fomit-frame-pointer -finline-functions>
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
        $<$<CONFIG:Release>:/O2 /Oi /Ot /Gy /GL /fp:fast /DNDEBUG>
        $<$<CONFIG:Debug>:/W4 /Zi /Od>
    )
    set_target_properties(${PROJECT_NAME} PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )
endif()

if(IS_MACOS)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@executable_path/../lib"
    )
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin/lib
        COMMAND ${CMAKE_COMMAND} -E echo "Copying libraries to bin directory..."
    )
endif()