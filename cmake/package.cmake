get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(NOT DEFINED VERSION)
  set(VERSION "1.0.0")
endif()
if(NOT DEFINED BIN_DIR)
  set(BIN_DIR "${PROJECT_ROOT}/bin")
endif()
if(NOT DEFINED OUTPUT_DIR)
  set(OUTPUT_DIR "${PROJECT_ROOT}/dist")
endif()

set(APP_NAME "Rind")

# Detect platform
if(CMAKE_HOST_WIN32)
  set(PLATFORM "windows")
elseif(CMAKE_HOST_APPLE)
  set(PLATFORM "macos")
else()
  set(PLATFORM "linux")
endif()

message(STATUS "Packaging for: ${PLATFORM}")
message(STATUS "Bin dir:       ${BIN_DIR}")
message(STATUS "Output dir:    ${OUTPUT_DIR}")

# Sanity check: binary must exist
if(PLATFORM STREQUAL "windows")
  set(_EXPECTED_BIN "${BIN_DIR}/${APP_NAME}.exe")
else()
  set(_EXPECTED_BIN "${BIN_DIR}/${APP_NAME}")
endif()
if(NOT EXISTS "${_EXPECTED_BIN}")
  message(FATAL_ERROR "Built binary not found: ${_EXPECTED_BIN}\nBuild the project in Release mode first.")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")


# macOS app bundle
if(PLATFORM STREQUAL "macos")
  set(APP_BUNDLE  "${OUTPUT_DIR}/${APP_NAME}.app")
  set(MACOS_DIR   "${APP_BUNDLE}/Contents/MacOS")
  set(PLIST_PATH  "${APP_BUNDLE}/Contents/Info.plist")

  message(STATUS "Creating ${APP_NAME}.app ...")
  file(REMOVE_RECURSE "${APP_BUNDLE}")
  file(MAKE_DIRECTORY "${MACOS_DIR}")

  # Copy the Mach-O binary directly — the binary uses _NSGetExecutablePath to
  # locate and set VK_ICD_FILENAMES at startup, so no shell wrapper is needed.
  file(COPY "${BIN_DIR}/${APP_NAME}" DESTINATION "${MACOS_DIR}")
  file(CHMOD "${MACOS_DIR}/${APP_NAME}"
       PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                   GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

  # Bundled dylibs and Vulkan ICD
  if(EXISTS "${BIN_DIR}/lib")
    file(COPY "${BIN_DIR}/lib" DESTINATION "${MACOS_DIR}")
  endif()
  if(EXISTS "${BIN_DIR}/icd")
    file(COPY "${BIN_DIR}/icd" DESTINATION "${MACOS_DIR}")
  endif()

  # Optional icon
  set(_ICNS "${PROJECT_ROOT}/cmake/${APP_NAME}.icns")
  if(EXISTS "${_ICNS}")
    file(MAKE_DIRECTORY "${APP_BUNDLE}/Contents/Resources")
    file(COPY "${_ICNS}" DESTINATION "${APP_BUNDLE}/Contents/Resources")
    set(_ICON_KEY "  <key>CFBundleIconFile</key><string>${APP_NAME}</string>\n")
  else()
    set(_ICON_KEY "")
  endif()

  file(WRITE "${PLIST_PATH}"
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\""
    " \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\">\n"
    "<dict>\n"
    "  <key>CFBundleExecutable</key><string>${APP_NAME}</string>\n"
    "  <key>CFBundleIdentifier</key><string>com.rind.game</string>\n"
    "  <key>CFBundleName</key><string>${APP_NAME}</string>\n"
    "  <key>CFBundleVersion</key><string>${VERSION}</string>\n"
    "  <key>CFBundleShortVersionString</key><string>${VERSION}</string>\n"
    "  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>\n"
    "  <key>CFBundlePackageType</key><string>APPL</string>\n"
    "  <key>CFBundleSignature</key><string>????</string>\n"
    "  <key>NSHighResolutionCapable</key><true/>\n"
    "${_ICON_KEY}"
    "</dict>\n"
    "</plist>\n"
  )

  message(STATUS "Done: ${APP_BUNDLE}")

# Linux AppImage
elseif(PLATFORM STREQUAL "linux")
  find_program(APPIMAGETOOL appimagetool)
  if(NOT APPIMAGETOOL)
    message(FATAL_ERROR
      "appimagetool not found.\n"
      "Download from https://github.com/AppImage/AppImageKit/releases "
      "and place it on PATH, e.g.:\n"
      "  chmod +x appimagetool-x86_64.AppImage\n"
      "  sudo mv appimagetool-x86_64.AppImage /usr/local/bin/appimagetool"
    )
  endif()

  set(APPDIR     "${OUTPUT_DIR}/${APP_NAME}.AppDir")
  set(APPIMAGE   "${OUTPUT_DIR}/${APP_NAME}-${VERSION}.AppImage")

  message(STATUS "Creating ${APP_NAME}.AppImage ...")
  file(REMOVE_RECURSE "${APPDIR}")
  file(MAKE_DIRECTORY "${APPDIR}/usr/bin")
  file(MAKE_DIRECTORY "${APPDIR}/usr/lib")

  file(COPY "${BIN_DIR}/${APP_NAME}" DESTINATION "${APPDIR}/usr/bin")
  file(CHMOD "${APPDIR}/usr/bin/${APP_NAME}"
       PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                   GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

  if(EXISTS "${BIN_DIR}/lib")
    file(GLOB _SO_FILES "${BIN_DIR}/lib/*.so*")
    foreach(_f IN LISTS _SO_FILES)
      file(COPY "${_f}" DESTINATION "${APPDIR}/usr/lib")
    endforeach()
  endif()

  # AppRun sets LD_LIBRARY_PATH so bundled libs are found
  file(WRITE "${APPDIR}/AppRun"
    "#!/bin/bash\n"
    "HERE=\"$(dirname \"$(readlink -f \"$0\")\")\"\n"
    "export LD_LIBRARY_PATH=\"$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\"\n"
    "exec \"$HERE/usr/bin/${APP_NAME}\" \"$@\"\n"
  )
  file(CHMOD "${APPDIR}/AppRun"
       PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                   GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

  # .desktop entry (required by AppImage spec)
  string(TOLOWER "${APP_NAME}" _APP_NAME_LOWER)
  file(WRITE "${APPDIR}/${APP_NAME}.desktop"
    "[Desktop Entry]\n"
    "Name=${APP_NAME}\n"
    "Exec=${APP_NAME}\n"
    "Icon=${_APP_NAME_LOWER}\n"
    "Type=Application\n"
    "Categories=Game;\n"
  )

  # Optional icon
  set(_ICON_SRC "${PROJECT_ROOT}/cmake/${_APP_NAME_LOWER}.png")
  if(EXISTS "${_ICON_SRC}")
    file(COPY "${_ICON_SRC}" DESTINATION "${APPDIR}")
  else()
    message(WARNING
      "No icon found at cmake/${_APP_NAME_LOWER}.png — AppImage will lack an icon.\n"
      "Add a 256x256 PNG at that path to include one."
    )
  endif()

  execute_process(
    COMMAND "${APPIMAGETOOL}" "${APPDIR}" "${APPIMAGE}"
    RESULT_VARIABLE _RESULT
  )
  if(NOT _RESULT EQUAL 0)
    message(FATAL_ERROR "appimagetool failed (exit ${_RESULT})")
  endif()

  message(STATUS "Done: ${APPIMAGE}")

# Windows distributable folder + zip
elseif(PLATFORM STREQUAL "windows")
  set(DIST_DIR "${OUTPUT_DIR}/${APP_NAME}-${VERSION}-windows")
  set(ZIP_OUT  "${OUTPUT_DIR}/${APP_NAME}-${VERSION}-windows.zip")

  message(STATUS "Creating Windows distributable ...")
  file(REMOVE_RECURSE "${DIST_DIR}")
  file(MAKE_DIRECTORY "${DIST_DIR}")

  file(COPY "${BIN_DIR}/${APP_NAME}.exe" DESTINATION "${DIST_DIR}")

  file(GLOB _DLLS "${BIN_DIR}/*.dll")
  foreach(_dll IN LISTS _DLLS)
    file(COPY "${_dll}" DESTINATION "${DIST_DIR}")
  endforeach()

  get_filename_component(_DIST_NAME "${DIST_DIR}" NAME)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${ZIP_OUT}" --format=zip -- "${_DIST_NAME}"
    WORKING_DIRECTORY "${OUTPUT_DIR}"
    RESULT_VARIABLE _RESULT
  )
  if(_RESULT EQUAL 0)
    message(STATUS "Done: ${ZIP_OUT}")
  else()
    message(WARNING "Zip failed - folder is still available at ${DIST_DIR}")
    message(STATUS "Done: ${DIST_DIR}")
  endif()
endif()
