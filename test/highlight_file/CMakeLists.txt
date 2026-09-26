# Only the pure decision logic (util/HighlightFileAction.h) is built here.
# HighlightFile.cpp has no host suite yet; test/storage_io/ shows how to build
# a store against the Storage fake in test/stubs. The .tmp promotion rule it
# used to hold now lives in test/temp_adoption/.
add_executable(HighlightFileActionTest
  HighlightFileActionTest.cpp
)

target_include_directories(HighlightFileActionTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Serialization
)

target_link_libraries(HighlightFileActionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HighlightFileActionTest)
