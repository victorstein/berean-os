add_executable(MigrationPlannerTest
  MigrationPlannerTest.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/MigrationPlanner.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/UnitAnchors.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/Unit.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/UnitFingerprint.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/TagPalette.cpp
  ${REPO_ROOT}/lib/Epub/Epub/VerseAnchors.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParagraphAnchors.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(MigrationPlannerTest PRIVATE
  ${REPO_ROOT}/lib/StudyStore
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/lib/Serialization
)

target_compile_definitions(MigrationPlannerTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=1024)

target_link_libraries(MigrationPlannerTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(MigrationPlannerTest)
