# Runs the real store I/O -- PersistableStore.cpp's atomic write and adopting
# read, and TagPaletteFile's load/save -- against the in-memory Storage fake in
# test/stubs. Modelled on test/pagination: real lib/ and src/ sources compiled
# unmodified, with link-time fakes supplying what the host lacks.
#
# test/stubs must come first on the include path so <Arduino.h>, <Logging.h> and
# <HalStorage.h> resolve to the stubs, never to lib/hal or the Arduino core.
add_executable(StorageIoTest
  HalStorageFakeTest.cpp
  ${REPO_ROOT}/test/stubs/HalStorageFake.cpp
)

target_include_directories(StorageIoTest PRIVATE
  ${REPO_ROOT}/test/stubs
)

target_link_libraries(StorageIoTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(StorageIoTest)
