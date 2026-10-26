# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

# Find the CEF binary distribution and build its C++ wrapper (libcef_dll_wrapper) with the static C runtime.
#
# Include this file in the CMakeLists.txt of the helper, not in the root CMakeLists.txt: the CMake files of CEF clear
# CMAKE_CXX_FLAGS, and that change must stay in the folder of the helper.
#
# After the include, TS4_CEF_FOUND is ON if the distribution is available. Then these exist:
# - the target libcef_dll_wrapper (a static library) and the imported target ts4_libcef (libcef.lib);
# - TS4_CEF_DEFINES: the compile definitions that a target that includes the CEF headers must use;
# - TS4_CEF_RUNTIME_FILES and TS4_CEF_RESOURCE_FILES: the files that go next to the helper exe;
# - CEF_BINARY_DIR_RELEASE, CEF_RESOURCE_DIR and CEF_INCLUDE_PATH from the CEF files.

set(TS4_CEF_VERSION "156.0.14+g4fa4ab5+chromium-156.0.8078.48")
# The default folder is in the cache of the workspace: the folder that holds projects. TS4_WORKSPACE can give it. Else
# it is two folders above the source folder, because the source is <workspace>/projects/<repo> or
# <workspace>/worktrees/<name>.
if (DEFINED ENV{TS4_WORKSPACE})
  file(TO_CMAKE_PATH "$ENV{TS4_WORKSPACE}" _ts4_workspace)
else ()
  get_filename_component(_ts4_workspace "${CMAKE_SOURCE_DIR}/../.." ABSOLUTE)
endif ()
set(TS4_CEF_ROOT
    "${_ts4_workspace}/cache/cef/${TS4_CEF_VERSION}"
    CACHE PATH "The folder of the CEF binary distribution (windows64, minimal) for the helper youtube_tv_host"
)
set(TS4_CEF_FOUND OFF)

if (NOT WIN32)
  message(STATUS "youtube_tv_host: the helper needs Windows. The build does not make it.")
  return()
endif ()

if (NOT EXISTS "${TS4_CEF_ROOT}/cmake/FindCEF.cmake" OR NOT EXISTS "${TS4_CEF_ROOT}/Release/libcef.lib")
  message(
    STATUS
      "youtube_tv_host: no CEF binary distribution in \"${TS4_CEF_ROOT}\". The build does not make the helper. "
      "Get the distribution of docs/cef.md and set TS4_CEF_ROOT to its folder."
  )
  return()
endif ()

# The helper runs without the sandbox of Chromium. With the sandbox, CEF needs bootstrap.exe and a client DLL in place
# of a normal exe. ATL is not necessary.
set(CEF_ROOT "${TS4_CEF_ROOT}")
set(USE_SANDBOX OFF CACHE BOOL "Use the sandbox of Chromium" FORCE)
set(USE_ATL OFF CACHE BOOL "Use ATL in the CEF wrapper" FORCE)
list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

# Our code uses C++ exceptions and RTTI. Compile the wrapper with the same settings, so that the two sides agree on the
# inline functions of the standard library.
list(REMOVE_ITEM CEF_COMPILER_FLAGS /GR-)
list(APPEND CEF_CXX_COMPILER_FLAGS /EHsc)
list(REMOVE_ITEM CEF_COMPILER_DEFINES _HAS_EXCEPTIONS=0)

# CMAKE_MSVC_RUNTIME_LIBRARY of the root gives /MT to the wrapper. The flag of CEF is then not necessary.
set(CEF_RUNTIME_LIBRARY_FLAG "" CACHE STRING "The runtime flag of CEF; empty, because CMake sets /MT" FORCE)
list(REMOVE_ITEM CEF_COMPILER_FLAGS_RELEASE /MT)
list(REMOVE_ITEM CEF_COMPILER_FLAGS_DEBUG /MTd)

add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" "${CMAKE_BINARY_DIR}/libcef_dll_wrapper" EXCLUDE_FROM_ALL)

add_library(ts4_libcef STATIC IMPORTED)
set_target_properties(ts4_libcef PROPERTIES IMPORTED_LOCATION "${CEF_LIB_RELEASE}")

set(TS4_CEF_DEFINES ${CEF_COMPILER_DEFINES})

# Only the files that the helper needs: no bootstrap.exe, because the helper does not use the sandbox.
set(TS4_CEF_RUNTIME_FILES ${CEF_BINARY_FILES})
set(TS4_CEF_RESOURCE_FILES ${CEF_RESOURCE_FILES})

# The version of the distribution that the build uses, from its header. The helper sends it in the hello event.
file(STRINGS "${CEF_ROOT}/include/cef_version.h" _cef_version_line REGEX "^#define CEF_VERSION \"")
string(REGEX REPLACE "^#define CEF_VERSION \"([^\"]+)\".*$" "\\1" TS4_CEF_FOUND_VERSION "${_cef_version_line}")
if (NOT TS4_CEF_FOUND_VERSION STREQUAL TS4_CEF_VERSION)
  message(WARNING "youtube_tv_host: the CEF in TS4_CEF_ROOT is ${TS4_CEF_FOUND_VERSION}, not ${TS4_CEF_VERSION}")
endif ()

message(STATUS "youtube_tv_host: CEF ${TS4_CEF_FOUND_VERSION} in \"${TS4_CEF_ROOT}\"")
set(TS4_CEF_FOUND ON)
