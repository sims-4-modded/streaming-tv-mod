# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

# The YouTube TV mod of The Sims 4: a DLL mod for the extender (the mod loader). It shows a YouTube video on the screen
# of a TV, with the audio at the position of the TV. The DLL uses only the static C runtime and static libraries.

cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

project(
  ts4_youtube_tv
  VERSION 0.1.0
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(TS4_YOUTUBE_TV_BUILD_TESTS "Build the tests of the YouTube TV" ON)

# The shared code (git submodule).
add_subdirectory(external/common)

find_package(nlohmann_json CONFIG REQUIRED)

# The logic of the mod that does not need the game: the settings, the URL of a video, and the protocol between the
# helper and its parent (the commands, the events, the shared memory, the audio ring and the I420 frames).
add_library(
  ts4_youtube_tv_core STATIC
  src/core/AudioRing.h
  src/core/AudioSpace.h
  src/core/BridgeArgs.h
  src/core/CodeOffsets.h
  src/core/FrameWriter.h
  src/core/GameObjects.h
  src/core/HostCommand.h
  src/core/HostProtocol.h
  src/core/PlayerPage.h
  src/core/Sessions.h
  src/core/SpeedPolicy.h
  src/core/Yuv.h
  src/core/YoutubeTvSettings.h
  src/core/VideoLayout.h
  src/core/YoutubeUrl.h
  src/core/AudioRing.cpp
  src/core/AudioSpace.cpp
  src/core/BridgeArgs.cpp
  src/core/CodeOffsets.cpp
  src/core/FrameWriter.cpp
  src/core/GameObjects.cpp
  src/core/HostCommand.cpp
  src/core/HostProtocol.cpp
  src/core/PlayerPage.cpp
  src/core/Sessions.cpp
  src/core/SpeedPolicy.cpp
  src/core/Yuv.cpp
  src/core/YoutubeTvSettings.cpp
  src/core/VideoLayout.cpp
  src/core/YoutubeUrl.cpp
)
target_include_directories(ts4_youtube_tv_core PUBLIC src)
target_link_libraries(ts4_youtube_tv_core PUBLIC ts4_common ts4_mod_core nlohmann_json::nlohmann_json)
ts4_set_warnings(ts4_youtube_tv_core)

if (WIN32)
  # The parts that use Windows: the helper client, the 3D audio, the memory of the process and the runtime. The DLL
  # and the probe tool use them.
  add_library(
    ts4_youtube_tv_game STATIC
    src/game/AudioEngine.h
    src/game/HostClient.h
    src/game/ProcessMemory.h
    src/game/TvRuntime.h
    src/game/WinHandle.h
    src/game/AudioEngine.cpp
    src/game/HostClient.cpp
    src/game/ProcessMemory.cpp
    src/game/TvRuntime.cpp
  )
  target_include_directories(ts4_youtube_tv_game PUBLIC src)
  target_compile_definitions(ts4_youtube_tv_game PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(ts4_youtube_tv_game PUBLIC ts4_youtube_tv_core ts4_mod_api)
  ts4_set_warnings(ts4_youtube_tv_game)

  add_library(youtube_tv SHARED src/game/Bridge.cpp src/game/YoutubeTvMod.cpp src/game/YoutubeTvMod.h src/game/ModMain.cpp)
  target_include_directories(youtube_tv PRIVATE src)
  target_link_libraries(youtube_tv PRIVATE ts4_youtube_tv_game ts4_mod_sdk ts4_mod_game)
  ts4_set_warnings(youtube_tv)

  # The helper process with CEF. It needs the CEF distribution of cmake/Cef.cmake; without it the build skips it.
  add_subdirectory(src/host)

  # The test client of the helper. It needs no CEF.
  add_subdirectory(tools/host_probe)
endif ()

if (TS4_YOUTUBE_TV_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif ()
