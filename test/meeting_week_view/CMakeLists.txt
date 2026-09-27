# The meetings screen's pure view model, beside its activity the way
# test/launcher_refresh tests LauncherRefresh.h. It needs the calendar helpers
# in WolWeekScan.cpp and catalog::wordAt/copyOut from CatalogLabel.cpp.
add_executable(MeetingWeekViewTest
  MeetingWeekViewTest.cpp
  ${REPO_ROOT}/src/activities/network/MeetingWeekView.cpp
  ${REPO_ROOT}/src/network/WolWeekScan.cpp
  ${REPO_ROOT}/lib/Catalog/Catalog/CatalogLabel.cpp
)

target_include_directories(MeetingWeekViewTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Catalog
)

target_link_libraries(MeetingWeekViewTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MeetingWeekViewTest)
