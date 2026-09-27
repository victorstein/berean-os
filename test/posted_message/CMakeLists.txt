# PostedMessage.cpp draws through GUI and reads millis(); only the pure cores
# are host-built: the queue and timing (activities/PostedMessageQueue.h) and
# where the toast it draws is anchored (components/ToastLayout.h).
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

add_executable(ToastLayoutTest
  ToastLayoutTest.cpp
)

target_include_directories(ToastLayoutTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ToastLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ToastLayoutTest)
