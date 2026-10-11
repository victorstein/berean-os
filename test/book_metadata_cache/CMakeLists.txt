# Runs the real BookMetadataCache against the in-memory Storage fake in test/stubs. Modelled on
# test/storage_io (real sources, test/stubs first on the include path) and test/pagination (a
# LINK-TIME substitute, here ZipFileStub.cpp, for a class the suite must not pull in).
#
# shim/WString.h lets FsHelpers.h reach the stub String; shim/PrintForward.h is force-included
# because ZipFile.h names Print, which the stub HalStorage.h does not declare.
add_executable(BookMetadataCacheTest
  BookMetadataCacheTest.cpp
  ZipFileStub.cpp
  ${REPO_ROOT}/test/stubs/HalStorageFake.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookMetadataCache.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(BookMetadataCacheTest PRIVATE
  ${REPO_ROOT}/test/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}/shim
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/ZipFile
)

target_compile_options(BookMetadataCacheTest PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/shim/PrintForward.h)

target_link_libraries(BookMetadataCacheTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookMetadataCacheTest)
