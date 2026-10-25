find_package(GTest CONFIG REQUIRED)

add_executable(ts4_youtube_tv_tests test_AudioRing.cpp test_AudioSpace.cpp test_BridgeArgs.cpp test_CodeOffsets.cpp test_FrameWriter.cpp test_GameObjects.cpp test_HostProtocol.cpp test_Sessions.cpp test_YoutubeTvSettings.cpp test_YoutubeUrl.cpp test_Yuv.cpp)
target_link_libraries(ts4_youtube_tv_tests PRIVATE ts4_youtube_tv_core GTest::gtest_main)
ts4_set_warnings(ts4_youtube_tv_tests)

include(GoogleTest)
gtest_discover_tests(ts4_youtube_tv_tests DISCOVERY_MODE PRE_TEST)

# The tests of the script mod. They run with Python 3.7, because the game runs Python 3.7, and they use fake game
# modules from tests/python/fakes. The build tool and the install tool also run their self-tests.
set(TS4_PYTHON37_HINT "C:/lib/python37" CACHE PATH "The folder of Python 3.7 for the script mod")
find_program(TS4_PYTHON37 NAMES python.exe python PATHS "${TS4_PYTHON37_HINT}" NO_DEFAULT_PATH)
if (TS4_PYTHON37)
  add_test(NAME script_mod.unittest
           COMMAND "${TS4_PYTHON37}" -B -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/python" -p "test_*.py"
           WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/python")
  add_test(NAME script_mod.build_self_test COMMAND "${TS4_PYTHON37}" -B "${PROJECT_SOURCE_DIR}/tools/build_script_mod.py"
                                                   --self-test)
  add_test(NAME script_mod.install_self_test
           COMMAND "${TS4_PYTHON37}" -B "${PROJECT_SOURCE_DIR}/tools/install_script_mod.py" --self-test)
else ()
  message(STATUS "Python 3.7 is not in ${TS4_PYTHON37_HINT}: the tests of the script mod do not run")
endif ()
