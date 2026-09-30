# The crop and sizing arithmetic behind components/CoverBand, tested the way
# test/posted_message tests components/ToastLayout.h: ${REPO_ROOT}/src is the
# only extra include directory, and the header under test includes nothing but
# <algorithm>, so no test/stubs entry is needed.
add_executable(CoverBandGeometryTest
  CoverBandGeometryTest.cpp
)

target_include_directories(CoverBandGeometryTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(CoverBandGeometryTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CoverBandGeometryTest)
