# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

# The local probe of the runtime of the DLL mod. It is not a test of CTest, because it needs the helper, the network
# and an audio device. It goes into the folder "tools" of the build folder.
add_executable(tv_probe TvProbe.cpp)
target_compile_definitions(tv_probe PRIVATE UNICODE _UNICODE)
target_link_libraries(tv_probe PRIVATE ts4_youtube_tv_game)
set_target_properties(tv_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
ts4_set_warnings(tv_probe)
