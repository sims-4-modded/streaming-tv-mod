find_package(GTest CONFIG REQUIRED)

add_executable(ts4_youtube_tv_tests test_AudioRing.cpp test_AudioSpace.cpp test_BridgeArgs.cpp test_CodeOffsets.cpp test_FrameWriter.cpp test_GameObjects.cpp test_HostProtocol.cpp test_PlaylistMessage.cpp test_Sessions.cpp test_ThreadGate.cpp test_YoutubeTvSettings.cpp test_YoutubeUrl.cpp test_Yuv.cpp)
target_link_libraries(ts4_youtube_tv_tests PRIVATE ts4_youtube_tv_core GTest::gtest_main)
ts4_set_warnings(ts4_youtube_tv_tests)

include(GoogleTest)
gtest_discover_tests(ts4_youtube_tv_tests DISCOVERY_MODE PRE_TEST)

# The tests of the script mod. They run with Python 3.7, because the game runs Python 3.7, and they use fake game
# modules from tests/python/fakes. The build tool and the install tool also run their self-tests.
# The environment variable TS4_PYTHON37 gives the folder of Python 3.7. If it is not set, CMake asks the Python
# launcher (py -3.7). The cache variable TS4_PYTHON37 keeps the python.exe that CMake found.
if (NOT TS4_PYTHON37)
  if (DEFINED ENV{TS4_PYTHON37})
    find_program(TS4_PYTHON37 NAMES python.exe python PATHS "$ENV{TS4_PYTHON37}" NO_DEFAULT_PATH)
  else ()
    find_program(_ts4_py_launcher NAMES py)
    if (_ts4_py_launcher)
      execute_process(
        COMMAND "${_ts4_py_launcher}" -3.7 -c "import sys; print(sys.executable)"
        OUTPUT_VARIABLE _ts4_python37_exe
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _ts4_python37_result
        ERROR_QUIET
      )
      if (_ts4_python37_result EQUAL 0 AND EXISTS "${_ts4_python37_exe}")
        file(TO_CMAKE_PATH "${_ts4_python37_exe}" _ts4_python37_exe)
        set(TS4_PYTHON37 "${_ts4_python37_exe}" CACHE FILEPATH "The python.exe of Python 3.7 for the script mod" FORCE)
      endif ()
    endif ()
    unset(_ts4_py_launcher CACHE)
  endif ()
endif ()
if (TS4_PYTHON37)
  add_test(NAME script_mod.unittest
           COMMAND "${TS4_PYTHON37}" -B -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/python" -p "test_*.py"
           WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/python")
  add_test(NAME script_mod.build_self_test COMMAND "${TS4_PYTHON37}" -B "${PROJECT_SOURCE_DIR}/tools/build_script_mod.py"
                                                   --self-test)
  add_test(NAME script_mod.install_self_test
           COMMAND "${TS4_PYTHON37}" -B "${PROJECT_SOURCE_DIR}/tools/install_script_mod.py" --self-test)
else ()
  message(STATUS "CMake found no Python 3.7: the tests of the script mod do not run")
endif ()
