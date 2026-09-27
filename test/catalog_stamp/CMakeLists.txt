add_executable(CatalogStampTest
  CatalogStampTest.cpp
  ${REPO_ROOT}/lib/Catalog/Catalog/CatalogStamp.cpp
  ${REPO_ROOT}/lib/Catalog/Catalog/CatalogLabel.cpp
  ${REPO_ROOT}/lib/Catalog/Catalog/CatalogIndex.cpp
)

target_include_directories(CatalogStampTest PRIVATE ${REPO_ROOT}/lib/Catalog)

target_link_libraries(CatalogStampTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CatalogStampTest)

add_executable(CatalogLabelTest
  CatalogLabelTest.cpp
  ${REPO_ROOT}/lib/Catalog/Catalog/CatalogLabel.cpp
)

target_include_directories(CatalogLabelTest PRIVATE ${REPO_ROOT}/lib/Catalog)

target_link_libraries(CatalogLabelTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CatalogLabelTest)
