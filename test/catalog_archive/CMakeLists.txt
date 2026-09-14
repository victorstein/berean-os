add_executable(CatalogArchiveTest
  CatalogArchiveTest.cpp
  ${REPO_ROOT}/lib/Catalog/Catalog/CatalogArchive.cpp
)

target_include_directories(CatalogArchiveTest PRIVATE ${REPO_ROOT}/lib/Catalog)

target_link_libraries(CatalogArchiveTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CatalogArchiveTest)
