add_executable(BibleSearchFoldTest
  FoldTest.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/Fold.cpp
)

target_include_directories(BibleSearchFoldTest PRIVATE
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch
)

target_link_libraries(BibleSearchFoldTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BibleSearchFoldTest)
