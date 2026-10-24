find_package(GTest CONFIG REQUIRED)

add_executable(ts4_template_tests test_TemplateSettings.cpp)
target_link_libraries(ts4_template_tests PRIVATE ts4_template_core GTest::gtest_main)
ts4_set_warnings(ts4_template_tests)

include(GoogleTest)
gtest_discover_tests(ts4_template_tests DISCOVERY_MODE PRE_TEST)
