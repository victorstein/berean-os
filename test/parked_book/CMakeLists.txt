# The warm-open rules, header-only and Epub-free by design: ParkedBookOf
# (src/activities/reader/ParkedBookOf.h) and study_reuse::mayReuse (src/study/StudyReuse.h).
# Modelled on test/number_grid's BibleNavCacheTest.
add_executable(ParkedBookTest
  ParkedBookTest.cpp
)

target_include_directories(ParkedBookTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ParkedBookTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ParkedBookTest)
