# Home's layout, routing and verse-cache rules (issue #203), modelled on
# test/masthead: the headers under test include std headers and the theme
# tables only, so ${REPO_ROOT}/src and lib/StudyStore (for study::Unit) are the
# include directories and no activity is constructed.
add_executable(HomeLayoutTest
  HomeLayoutTest.cpp
  HomeTargetsTest.cpp
  HomeVerseCacheTest.cpp
)

target_include_directories(HomeLayoutTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/StudyStore
)

target_link_libraries(HomeLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HomeLayoutTest)

# Home's saved verse format (spec berean-o-7), modelled on test/places_doc: the pure half with
# ArduinoJson and study::Unit, no storage. Its own executable, so HomeLayoutTest keeps to std
# headers.
add_executable(HomeVerseDocTest
  HomeVerseDocTest.cpp
  ${REPO_ROOT}/src/activities/launcher/HomeVerseDoc.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/Unit.cpp
)

target_include_directories(HomeVerseDocTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/StudyStore
  ${REPO_ROOT}/lib/Serialization
)

target_link_libraries(HomeVerseDocTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(HomeVerseDocTest)

# Home's saved verse file against the in-memory HalStorage fake, modelled on test/places_store:
# test/stubs comes first on the include path, so HalStorage.h and Logging.h resolve to the stubs.
add_executable(HomeVerseFileTest
  HomeVerseFileTest.cpp
  ${REPO_ROOT}/src/activities/launcher/HomeVerseFile.cpp
  ${REPO_ROOT}/src/activities/launcher/HomeVerseDoc.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/Unit.cpp
  ${REPO_ROOT}/lib/Serialization/PersistableStore.cpp
  ${REPO_ROOT}/test/stubs/HalStorageFake.cpp
  ${REPO_ROOT}/test/stubs/ObfuscationUtilsStub.cpp
)

target_include_directories(HomeVerseFileTest PRIVATE
  ${REPO_ROOT}/test/stubs
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/StudyStore
)

target_link_libraries(HomeVerseFileTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(HomeVerseFileTest)
