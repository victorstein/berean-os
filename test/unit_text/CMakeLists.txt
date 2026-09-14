add_executable(UnitTextTest
  UnitTextTest.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/UnitText.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/UnitAnchors.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/UnitFingerprint.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/Unit.cpp
  ${REPO_ROOT}/lib/Epub/Epub/VerseAnchors.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParagraphAnchors.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(UnitTextTest PRIVATE
  ${REPO_ROOT}/lib/StudyStore
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(UnitTextTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=1024)

target_link_libraries(UnitTextTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(UnitTextTest)
