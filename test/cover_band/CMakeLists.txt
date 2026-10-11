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

# BandInk's layout arithmetic, header-only like the geometry above.
add_executable(BandInkTest
  BandInkTest.cpp
)

target_include_directories(BandInkTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(BandInkTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BandInkTest)

# A fresh CoverBand draw against its replay, on the real CoverBand,
# CoverBandCache and Bitmap code. GfxRenderer is a link-time fake
# (GfxRendererFake.cpp, after test/pagination). This directory comes first so it
# supplies <esp_heap_caps.h>; test/stubs supplies <HalStorage.h>, <HalDisplay.h>,
# <Logging.h> and <Arduino.h>.
add_executable(CoverBandReplayTest
  CoverBandReplayTest.cpp
  GfxRendererFake.cpp
  CoverThumbStub.cpp
  ${REPO_ROOT}/src/components/CoverBand.cpp
  ${REPO_ROOT}/src/components/CoverBandCache.cpp
  ${REPO_ROOT}/lib/GfxRenderer/Bitmap.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/test/stubs/HalStorageFake.cpp
)

target_include_directories(CoverBandReplayTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${REPO_ROOT}/test/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/MiniBidi
)

target_link_libraries(CoverBandReplayTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CoverBandReplayTest)
