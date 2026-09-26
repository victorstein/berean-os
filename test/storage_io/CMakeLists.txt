# Runs the real store I/O -- PersistableStore.cpp's atomic write, adopting read
# and loadAdopting, and the TagPaletteFile, ChapterCompletionFile and
# PassageFile loaders built on it -- against the in-memory Storage fake in
# test/stubs. Modelled on test/pagination: real lib/ and src/ sources compiled
# unmodified, with link-time fakes supplying what the host lacks.
#
# test/stubs must come first on the include path so <Arduino.h>, <Logging.h> and
# <HalStorage.h> resolve to the stubs, never to lib/hal or the Arduino core.
add_executable(StorageIoTest
  HalStorageFakeTest.cpp
  AtomicWriteTest.cpp
  AdoptingReadTest.cpp
  LoadAdoptingTest.cpp
  TagPaletteFileTest.cpp
  ChapterCompletionFileTest.cpp
  PassageFileTest.cpp
  ${REPO_ROOT}/test/stubs/HalStorageFake.cpp
  ${REPO_ROOT}/test/stubs/ObfuscationUtilsStub.cpp
  ${REPO_ROOT}/lib/Serialization/PersistableStore.cpp
  ${REPO_ROOT}/src/study/TagPaletteFile.cpp
  ${REPO_ROOT}/src/study/ChapterCompletionFile.cpp
  ${REPO_ROOT}/src/study/PassageFile.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/TagPalette.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/ChapterCompletion.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/PassageDoc.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/Unit.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/UnitFingerprint.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(StorageIoTest PRIVATE
  ${REPO_ROOT}/test/stubs
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/StudyStore
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/src/study
)

target_link_libraries(StorageIoTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(StorageIoTest)
