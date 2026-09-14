# expat is compiled here from the repo's own sources with the same flags
# platformio.ini passes, so the scan is tokenised by the identical parser the
# firmware uses.
add_executable(ParagraphAnchorsTest
  ParagraphAnchorsTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParagraphAnchors.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(ParagraphAnchorsTest PRIVATE
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(ParagraphAnchorsTest PRIVATE XML_GE=0 XML_CONTEXT_BYTES=1024)

target_link_libraries(ParagraphAnchorsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ParagraphAnchorsTest)
