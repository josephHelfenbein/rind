if(NOT DEFINED ZIG_GLIBC)
  set(ZIG_GLIBC "2.36")
endif()

set(_zig_dir "${CMAKE_BINARY_DIR}/_zig")
file(MAKE_DIRECTORY "${_zig_dir}")

set(_target "x86_64-linux-gnu.${ZIG_GLIBC}")

# -isystem /usr/include: zig cc doesn't search system include paths by default,
# needed for non-libc system headers
file(WRITE "${_zig_dir}/cc"  "#!/bin/sh\nexec zig cc  -target ${_target} -isystem /usr/include \"$@\"\n")
file(WRITE "${_zig_dir}/cxx" "#!/bin/sh\nexec zig c++ -target ${_target} -isystem /usr/include \"$@\"\n")
file(CHMOD "${_zig_dir}/cc"  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
file(CHMOD "${_zig_dir}/cxx" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)

set(CMAKE_C_COMPILER   "${_zig_dir}/cc")
set(CMAKE_CXX_COMPILER "${_zig_dir}/cxx")

# System shared libraries (e.g. libvulkan.so) reference the host glibc, not
# our target glibc.  These libraries are NOT bundled in the AppImage
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--allow-shlib-undefined")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-Wl,--allow-shlib-undefined")
