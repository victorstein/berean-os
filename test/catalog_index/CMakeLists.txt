add_executable(CatalogIndexTest
  CatalogIndexTest.cpp
  ${REPO_ROOT}/lib/Catalog/Catalog/CatalogIndex.cpp
)

target_include_directories(CatalogIndexTest PRIVATE ${REPO_ROOT}/lib/Catalog)

target_link_libraries(CatalogIndexTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CatalogIndexTest)
