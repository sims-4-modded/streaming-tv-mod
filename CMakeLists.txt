# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

# The template of a DLL mod of The Sims 4 for the extender (the mod loader). Copy it to start a new mod, then run
# tools/rename_mod.py. The DLL uses only the static C runtime and static libraries.

cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

project(
  ts4_mod_template
  VERSION 0.1.0
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(TS4_TEMPLATE_BUILD_TESTS "Build the tests of the template mod" ON)

# The shared code (git submodule).
add_subdirectory(external/common)

find_package(nlohmann_json CONFIG REQUIRED)

# The logic of the mod that does not need the game: the settings.
add_library(ts4_template_core STATIC src/core/TemplateSettings.h src/core/TemplateSettings.cpp)
target_include_directories(ts4_template_core PUBLIC src)
target_link_libraries(ts4_template_core PUBLIC ts4_common ts4_mod_core nlohmann_json::nlohmann_json)
ts4_set_warnings(ts4_template_core)

if (WIN32)
  add_library(mod_template SHARED src/game/TemplateMod.cpp src/game/TemplateMod.h src/game/ModMain.cpp)
  target_include_directories(mod_template PRIVATE src)
  target_compile_definitions(mod_template PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(mod_template PRIVATE ts4_template_core ts4_mod_sdk)
  ts4_set_warnings(mod_template)
endif ()

if (TS4_TEMPLATE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif ()
