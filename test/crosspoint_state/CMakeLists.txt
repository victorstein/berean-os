# Runs the real CrossPointState.cpp and PersistableStore.cpp against the
# in-memory HalStorage fake, which must come first on the include path, the way
# test/persistable_store does.
add_executable(CrossPointStateTest
  CrossPointStateTest.cpp
  ${REPO_ROOT}/src/CrossPointState.cpp
  ${REPO_ROOT}/lib/Serialization/PersistableStore.cpp
  ${REPO_ROOT}/test/stubs/HalStorageFake.cpp
  ${REPO_ROOT}/test/stubs/ObfuscationUtilsStub.cpp
)

target_include_directories(CrossPointStateTest PRIVATE
  ${REPO_ROOT}/test/stubs
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/src
)

target_link_libraries(CrossPointStateTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(CrossPointStateTest)
