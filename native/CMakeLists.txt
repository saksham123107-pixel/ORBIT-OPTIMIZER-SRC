cmake_minimum_required(VERSION 3.20)
project(primex-optimizer-cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── embed tweaks pack (pure CMake, hex, no external tools) ────────────────
set(TWEAKS_JSON "${CMAKE_CURRENT_SOURCE_DIR}/../shell/Optimizer/tweaks.json")
set(TWEAKS_EMBED_SRC "${CMAKE_CURRENT_BINARY_DIR}/generated/tweaks_embedded.cpp")
add_custom_command(
  OUTPUT "${TWEAKS_EMBED_SRC}"
  COMMAND ${CMAKE_COMMAND}
          -DJSON="${TWEAKS_JSON}"
          -DOUT="${TWEAKS_EMBED_SRC}"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_tweaks.cmake"
  DEPENDS "${TWEAKS_JSON}"
  COMMENT "Embedding tweaks pack into the exe..."
)

# ── embed auth.env (bot token lives inside the binary — no sidecar file) ──
set(AUTH_ENV "${CMAKE_CURRENT_SOURCE_DIR}/auth.env")
set(AUTH_EMBED_SRC "${CMAKE_CURRENT_BINARY_DIR}/generated/auth_embedded.cpp")
add_custom_command(
  OUTPUT "${AUTH_EMBED_SRC}"
  COMMAND ${CMAKE_COMMAND}
          -DENV="${AUTH_ENV}"
          -DOUT="${AUTH_EMBED_SRC}"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_auth.cmake"
  DEPENDS "${AUTH_ENV}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_auth.cmake"
  COMMENT "Embedding auth.env into the exe..."
)

# Engine: pure Win32 C++17, builds with MSVC or MinGW.
add_library(primex_core STATIC
  src/aim.cpp
  src/auth.cpp
  src/booster.cpp
  src/cleaner.cpp
  src/debloat.cpp
  src/guard.cpp
  src/hotkey.cpp
  src/system.cpp
  src/services.cpp
  src/tweaks.cpp
  src/primex_core.cpp
  "${TWEAKS_EMBED_SRC}"
  "${AUTH_EMBED_SRC}"
)
target_include_directories(primex_core PUBLIC include src)
if(WIN32)
  target_link_libraries(primex_core PUBLIC winhttp crypt32 psapi ws2_32 pdh)
  target_compile_definitions(primex_core PUBLIC UNICODE _UNICODE)
endif()

# Console smoke test — validates the engine without WebView2.
add_executable(px_selftest selftest.cpp)
target_link_libraries(px_selftest PRIVATE primex_core)

# Hash helper — generates the SHA-256 hex GitHub AUTH.json wants.
add_executable(px_hash EXCLUDE_FROM_ALL tools/hash/hash_tool.cpp)
target_include_directories(px_hash PRIVATE src)
target_link_libraries(px_hash PRIVATE crypt32)
if(MINGW)
  target_link_options(px_hash PRIVATE -municode)
endif()
target_compile_definitions(px_hash PRIVATE UNICODE _UNICODE)

# Anti-tamper patch tool: writes the exe's SHA-256 into guard.cpp's marker
# slot after every link. Pure C++ (see tools/selfhash/selfhash_tool.cpp).
add_executable(px_selfhash EXCLUDE_FROM_ALL tools/selfhash/selfhash_tool.cpp)
target_link_libraries(px_selfhash PRIVATE crypt32)
if(MINGW)
  target_link_options(px_selfhash PRIVATE -municode)
endif()
target_compile_definitions(px_selfhash PRIVATE UNICODE _UNICODE)

# Native host (borderless Win32 + WebView2). Works with MSVC and MinGW:
# no WRL, no static link — WebView2Loader.dll loads at runtime.
# SDK headers are vendored under thirdparty/ (NuGet Microsoft.Web.WebView2).
set(WV2_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/webview2/build/native/include")
set(WV2_LOADER "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/webview2/runtimes/win-x64/native/WebView2Loader.dll")
if(WIN32 AND EXISTS "${WV2_INCLUDE}/WebView2.h" AND EXISTS "${WV2_LOADER}")
  # admin manifest (same level as shell/app.manifest), via .rc so both
  # rc.exe (MSVC) and windres (MinGW) embed it.
  set(PX_MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/host/app.manifest")
  set(PX_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../assets/orbit.ico")
  configure_file("${PX_ICON_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/orbit.ico" COPYONLY)
  configure_file(host/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)

  set(UI_EMBED_H "${CMAKE_CURRENT_BINARY_DIR}/generated/ui_embedded.h")
  set(UI_EMBED_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated/ui_embedded.cpp")
  set(UI_DIST "${CMAKE_CURRENT_SOURCE_DIR}/../frontend/dist")
  set(UI_LOADER "${WV2_LOADER}")
  add_custom_command(
    OUTPUT "${UI_EMBED_H}" "${UI_EMBED_CPP}"
    COMMAND ${CMAKE_COMMAND}
            -DDIST="${UI_DIST}"
            -DLOADER="${UI_LOADER}"
            -DOUT_H="${UI_EMBED_H}"
            -DOUT_CPP="${UI_EMBED_CPP}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_ui.cmake"
    DEPENDS "${UI_DIST}" "${UI_LOADER}"
    COMMENT "Embedding UI + WebView2 loader into the exe..."
  )

  add_executable(primex-glass WIN32
    host/main.cpp
    host/ipc.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ui_embedded.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
  )
  # Ship as "ORBIT OPTIMIZER.exe" (not a standalone glass exe).
  set_target_properties(primex-glass PROPERTIES OUTPUT_NAME "ORBIT OPTIMIZER")
  target_include_directories(primex-glass PRIVATE
    "${WV2_INCLUDE}" include src host
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_link_libraries(primex-glass PRIVATE primex_core
    shlwapi shell32 ole32 uuid dwmapi advapi32)
  if(MINGW)
    # Static MinGW runtimes: the shipped exe must run on machines without
    # libstdc++/libgcc/libwinpthread DLLs (installer payload = exe only).
    target_link_options(primex-glass PRIVATE -municode -static -static-libgcc -static-libstdc++)
  endif()
  target_compile_definitions(primex-glass PRIVATE UNICODE _UNICODE)
  add_dependencies(primex-glass px_selfhash)

  # Not a standalone exe: stage frontend/dist + WebView2Loader next to the
  # binary so the UI ships as files (FindDist prefers local dist over embed).
  add_custom_command(TARGET primex-glass POST_BUILD
    COMMAND $<TARGET_FILE:px_selfhash> "$<TARGET_FILE:primex-glass>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${UI_DIST}" "$<TARGET_FILE_DIR:primex-glass>/dist"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UI_LOADER}" "$<TARGET_FILE_DIR:primex-glass>/WebView2Loader.dll"
  )
  message(STATUS "ORBIT OPTIMIZER host enabled (WebView2 found).")
else()
  message(STATUS "WebView2 SDK not found under thirdparty/ — host skipped, engine still builds.")
endif()