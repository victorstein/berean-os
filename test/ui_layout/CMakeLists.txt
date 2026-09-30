add_executable(ListRowHeightTest
  ListRowHeightTest.cpp
)

target_include_directories(ListRowHeightTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ListRowHeightTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ListRowHeightTest)

add_executable(BibleReferenceTest
  BibleReferenceTest.cpp
)

target_include_directories(BibleReferenceTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(BibleReferenceTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BibleReferenceTest)

add_executable(TypedReferenceTest
  TypedReferenceTest.cpp
  ${REPO_ROOT}/src/activities/reader/TypedReference.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/Fold.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/IndexFormat.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/IndexBuilder.cpp
  ${REPO_ROOT}/lib/BibleSearch/BibleSearch/IndexReader.cpp
)

target_include_directories(TypedReferenceTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/BibleSearch
)

target_link_libraries(TypedReferenceTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TypedReferenceTest)

add_executable(ReaderMenuModelTest
  ReaderMenuModelTest.cpp
)

target_include_directories(ReaderMenuModelTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ReaderMenuModelTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReaderMenuModelTest)
