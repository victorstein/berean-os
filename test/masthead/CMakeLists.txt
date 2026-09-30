# The masthead's layout arithmetic (components/MastheadLayout.h) against the
# real theme tables, modelled on test/cover_band: ${REPO_ROOT}/src is the only
# include directory, and every header under test includes std headers only.
add_executable(MastheadLayoutTest
  MastheadLayoutTest.cpp
)

target_include_directories(MastheadLayoutTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(MastheadLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MastheadLayoutTest)
