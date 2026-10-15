# expat is compiled here from the repo's own sources with the same flags
# platformio.ini passes, so a question file is tokenised by the identical parser
# the firmware uses. xmltok_impl.c and xmltok_ns.c are #included by xmltok.c.
add_executable(PreachingTest
  IntroSectionTest.cpp
  QuestionParserTest.cpp
  ${REPO_ROOT}/lib/Preaching/Preaching/IntroSection.cpp
  ${REPO_ROOT}/lib/Preaching/Preaching/QuestionParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(PreachingTest PRIVATE
  ${REPO_ROOT}/lib/Preaching
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/test/stubs  # no-op Logging.h
)

target_compile_definitions(PreachingTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=1024)

target_link_libraries(PreachingTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PreachingTest)
