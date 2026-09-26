# Links the real PersistableStore.cpp against the in-memory HalStorage fake in
# test/stubs, which must come first on the include path.
add_executable(PersistableStoreTest
  PersistableStoreTest.cpp
  ${REPO_ROOT}/lib/Serialization/PersistableStore.cpp
  ${REPO_ROOT}/test/stubs/HalStorageFake.cpp
  ${REPO_ROOT}/test/stubs/ObfuscationUtilsStub.cpp
)

target_include_directories(PersistableStoreTest PRIVATE
  ${REPO_ROOT}/test/stubs
  ${REPO_ROOT}/lib/Serialization
)

target_link_libraries(PersistableStoreTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(PersistableStoreTest)
