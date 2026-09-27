# Tested the way test/launcher_bible tests LauncherBible.h: a header-only helper
# beside the sleep activity, no activity constructed. Also links
# CatalogLabel.cpp for catalog::wordAt, the way test/catalog_stamp's
# CatalogLabelTest does.
add_executable(StudySleepPickTest
  StudySleepPickTest.cpp
  ${REPO_ROOT}/lib/Catalog/Catalog/CatalogLabel.cpp
)

target_include_directories(StudySleepPickTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Catalog
)

target_link_libraries(StudySleepPickTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(StudySleepPickTest)
