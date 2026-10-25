# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

# The helper youtube_tv_host.exe: CEF with windowless rendering and the YouTube IFrame Player API. The build puts the
# exe, the CEF runtime and the licences in the folder "host" of the build folder. The tool tools/install_host.py copies
# the files of youtube_tv_host_files.txt from there to the folder of the player.

include(${PROJECT_SOURCE_DIR}/cmake/Cef.cmake)
if (NOT TS4_CEF_FOUND)
  return()
endif ()

set(TS4_HOST_OUTPUT_DIR "${CMAKE_BINARY_DIR}/host")

add_executable(
  youtube_tv_host WIN32
  EventWriter.h
  Host.h
  HostApp.h
  Resource.h
  VideoSession.h
  WinHandle.h
  EventWriter.cpp
  Host.cpp
  HostApp.cpp
  Main.cpp
  VideoSession.cpp
  WinHandle.cpp
  YoutubeTvHost.rc
  youtube_tv_host.manifest
)
target_include_directories(youtube_tv_host PRIVATE ${PROJECT_SOURCE_DIR}/src)
# The CEF headers are not ours: the compiler does not give warnings for them.
target_include_directories(youtube_tv_host SYSTEM PRIVATE ${CEF_INCLUDE_PATH})
target_compile_definitions(youtube_tv_host PRIVATE ${TS4_CEF_DEFINES})
# The folder "cmake" of CEF clears CMAKE_CXX_FLAGS in this folder: give the exceptions back.
target_compile_options(youtube_tv_host PRIVATE /EHsc)
target_link_libraries(youtube_tv_host PRIVATE ts4_youtube_tv_core libcef_dll_wrapper ts4_libcef ${CEF_STANDARD_LIBS})
# Chromium needs a large stack on the main thread.
target_link_options(youtube_tv_host PRIVATE /STACK:0x800000)
set_target_properties(youtube_tv_host PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${TS4_HOST_OUTPUT_DIR}")
# The resource compiler finds player.html next to the .rc file.
set_source_files_properties(YoutubeTvHost.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/player.html")
ts4_set_warnings(youtube_tv_host)

# Copy the CEF runtime and the licences next to the exe.
set(_host_files youtube_tv_host.exe)
foreach (file IN LISTS TS4_CEF_RUNTIME_FILES)
  add_custom_command(
    TARGET youtube_tv_host POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CEF_BINARY_DIR_RELEASE}/${file}"
                                              "${TS4_HOST_OUTPUT_DIR}/${file}"
  )
  list(APPEND _host_files ${file})
endforeach ()
foreach (file IN LISTS TS4_CEF_RESOURCE_FILES)
  if (IS_DIRECTORY "${CEF_RESOURCE_DIR}/${file}")
    add_custom_command(
      TARGET youtube_tv_host POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory "${CEF_RESOURCE_DIR}/${file}"
                                                "${TS4_HOST_OUTPUT_DIR}/${file}"
    )
    file(GLOB _children RELATIVE "${CEF_RESOURCE_DIR}" "${CEF_RESOURCE_DIR}/${file}/*")
    list(APPEND _host_files ${_children})
  else ()
    add_custom_command(
      TARGET youtube_tv_host POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CEF_RESOURCE_DIR}/${file}"
                                                "${TS4_HOST_OUTPUT_DIR}/${file}"
    )
    list(APPEND _host_files ${file})
  endif ()
endforeach ()
add_custom_command(
  TARGET youtube_tv_host
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${TS4_HOST_OUTPUT_DIR}/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CEF_ROOT}/LICENSE.txt" "${TS4_HOST_OUTPUT_DIR}/licenses/CEF_LICENSE.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CEF_ROOT}/CREDITS.html"
          "${TS4_HOST_OUTPUT_DIR}/licenses/CHROMIUM_CREDITS.html"
)
list(APPEND _host_files licenses/CEF_LICENSE.txt licenses/CHROMIUM_CREDITS.html)

# The list of the files that the install tool copies, one path on each line, relative to the folder "host".
list(JOIN _host_files "\n" _host_file_lines)
file(WRITE "${TS4_HOST_OUTPUT_DIR}/youtube_tv_host_files.txt" "${_host_file_lines}\n")
file(WRITE "${TS4_HOST_OUTPUT_DIR}/youtube_tv_host_version.txt" "cef ${TS4_CEF_FOUND_VERSION}\nproject ${PROJECT_VERSION}\n")
