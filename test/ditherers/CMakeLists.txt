# Host tests for the error-diffusion ditherers in lib/GfxRenderer/BitmapHelpers.h.
#
# test/stubs comes first so BitmapHelpers.cpp's <HalStorage.h> (via Bitmap.h)
# resolves to the host stub instead of the firmware HAL.
add_executable(DitherersTest
  DitherersTest.cpp
  FailingNothrowNew.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
)

target_include_directories(DitherersTest PRIVATE
  ${REPO_ROOT}/test/stubs
  ${REPO_ROOT}/lib
)

# Clang may omit a replaceable operator new called from a new-expression when
# the result is only null-checked, zeroed and freed, so an injected failure
# would never be observed. -fno-builtin keeps every call real.
target_compile_options(DitherersTest PRIVATE -fno-builtin)

target_link_libraries(DitherersTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DitherersTest)
