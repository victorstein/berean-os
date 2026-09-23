# PostedMessage.cpp draws through GUI and reads millis(); only the queue and
# timing core (activities/PostedMessageQueue.h) is host-built.
add_executable(PostedMessageQueueTest
  PostedMessageQueueTest.cpp
)

target_include_directories(PostedMessageQueueTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(PostedMessageQueueTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PostedMessageQueueTest)
