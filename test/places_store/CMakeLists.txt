# Runs the real PlacesStore.cpp and PersistableStore.cpp against the in-memory HalStorage fake,
# which must come first on the include path, the way test/crosspoint_state does. The lazy-load case
# is its own executable: the store is a process-wide singleton, and that case needs one that has
# never loaded.
set(PLACES_STORE_SOURCES
  ${REPO_ROOT}/src/PlacesStore.cpp
  ${REPO_ROOT}/src/util/PlacesDoc.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/Unit.cpp
  ${REPO_ROOT}/lib/StudyStore/StudyStore/UnitAnchors.cpp
  ${REPO_ROOT}/lib/Epub/Epub/VerseAnchors.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParagraphAnchors.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
  ${REPO_ROOT}/lib/Serialization/PersistableStore.cpp
  ${REPO_ROOT}/test/stubs/HalStorageFake.cpp
  ${REPO_ROOT}/test/stubs/ObfuscationUtilsStub.cpp
)

foreach(target PlacesStoreTest PlacesStoreLazyLoadTest)
  add_executable(${target} ${target}.cpp ${PLACES_STORE_SOURCES})
  target_include_directories(${target} PRIVATE
    ${REPO_ROOT}/test/stubs
    ${REPO_ROOT}/lib/Serialization
    ${REPO_ROOT}/lib/Memory
    ${REPO_ROOT}/src
    ${REPO_ROOT}/lib/Utf8
    ${REPO_ROOT}/lib/StudyStore
    ${REPO_ROOT}/lib/Epub
    ${REPO_ROOT}/lib/Epub/Epub
    ${REPO_ROOT}/lib/expat
  )
  target_compile_definitions(${target} PRIVATE XML_GE=0 XML_CONTEXT_BYTES=1024)
  target_link_libraries(${target} PRIVATE
    crosspoint_test_common
    ArduinoJson
    GTest::gtest_main
  )
  gtest_discover_tests(${target})
endforeach()
