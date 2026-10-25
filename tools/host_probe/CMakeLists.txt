# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

# The test client of the helper. It uses the protocol of src/core and only libraries of Windows. It goes into the
# folder "tools" of the build folder, and it finds the helper in the folder "host" next to it.
add_executable(host_probe HostProbe.cpp)
target_compile_definitions(host_probe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(host_probe PRIVATE ts4_youtube_tv_core ole32 windowscodecs)
set_target_properties(host_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
ts4_set_warnings(host_probe)
