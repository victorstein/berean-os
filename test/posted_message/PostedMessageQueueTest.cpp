// Host coverage for the queue and timing rules behind PostedMessage. The
// wrapper adds only a mutex, millis() and GUI.drawPopup.

#include <gtest/gtest.h>

#include "activities/PostedMessageQueue.h"

namespace {
constexpr uint32_t HOLD = PostedMessageQueue::MIN_DISPLAY_MS;
const char* const FIRST = "first";
const char* const SECOND = "second";
const char* const THIRD = "third";
}  // namespace

TEST(PostedMessageQueue, NothingPostedDrawsNothing) {
  PostedMessageQueue queue;
  EXPECT_EQ(queue.next(0), nullptr);
}

TEST(PostedMessageQueue, APostedMessageIsDrawnByTheNextRender) {
  PostedMessageQueue queue;
  EXPECT_EQ(queue.post(FIRST, 0), PostedMessageQueue::PostResult::Queued);
  EXPECT_EQ(queue.next(10), FIRST);
}

TEST(PostedMessageQueue, ADrawnMessageIsRedrawnUntilItsHoldRunsOut) {
  PostedMessageQueue queue;
  queue.post(FIRST, 0);
  ASSERT_EQ(queue.next(1000), FIRST);
  EXPECT_EQ(queue.next(1500), FIRST) << "a progress repaint must not wipe it";
  EXPECT_EQ(queue.next(1000 + HOLD - 1), FIRST);
  EXPECT_EQ(queue.next(1000 + HOLD), nullptr);
}

TEST(PostedMessageQueue, TheHoldCountsFromTheFirstDrawNotFromThePost) {
  PostedMessageQueue queue;
  queue.post(FIRST, 0);
  ASSERT_EQ(queue.next(HOLD * 4), FIRST) << "a message posted long before the next render still gets its hold";
  EXPECT_EQ(queue.next(HOLD * 4 + 1), FIRST);
}

TEST(PostedMessageQueue, TheNextMessageWaitsForTheCurrentOneToExpire) {
  PostedMessageQueue queue;
  queue.post(FIRST, 0);
  queue.post(SECOND, 0);
  ASSERT_EQ(queue.next(0), FIRST);
  EXPECT_EQ(queue.next(HOLD - 1), FIRST);
  EXPECT_EQ(queue.next(HOLD), SECOND);
  EXPECT_EQ(queue.next(HOLD * 2 - 1), SECOND);
  EXPECT_EQ(queue.next(HOLD * 2), nullptr);
}

TEST(PostedMessageQueue, TheSameMessageQueuedTwiceIsKeptOnce) {
  PostedMessageQueue queue;
  EXPECT_EQ(queue.post(FIRST, 0), PostedMessageQueue::PostResult::Queued);
  EXPECT_EQ(queue.post(FIRST, 0), PostedMessageQueue::PostResult::Duplicate);
  ASSERT_EQ(queue.next(0), FIRST);
  EXPECT_EQ(queue.next(HOLD), nullptr);
}

TEST(PostedMessageQueue, TheMessageOnScreenIsNotQueuedAgain) {
  PostedMessageQueue queue;
  queue.post(FIRST, 0);
  ASSERT_EQ(queue.next(0), FIRST);
  EXPECT_EQ(queue.post(FIRST, 100), PostedMessageQueue::PostResult::Duplicate);
  EXPECT_EQ(queue.next(HOLD), nullptr);
}

TEST(PostedMessageQueue, AnExpiredMessageCanBePostedAgain) {
  PostedMessageQueue queue;
  queue.post(FIRST, 0);
  ASSERT_EQ(queue.next(0), FIRST);
  EXPECT_EQ(queue.post(FIRST, HOLD), PostedMessageQueue::PostResult::Queued)
      << "a second failure later on is news, not a repeat";
  EXPECT_EQ(queue.next(HOLD), FIRST);
}

TEST(PostedMessageQueue, APostPastCapacityIsDropped) {
  PostedMessageQueue queue;
  queue.post(FIRST, 0);
  queue.post(SECOND, 0);
  EXPECT_EQ(queue.post(THIRD, 0), PostedMessageQueue::PostResult::Full);
}

TEST(PostedMessageQueue, TheShowingMessageDoesNotTakeAQueueSlot) {
  PostedMessageQueue queue;
  queue.post(FIRST, 0);
  ASSERT_EQ(queue.next(0), FIRST);
  EXPECT_EQ(queue.post(SECOND, 0), PostedMessageQueue::PostResult::Queued);
  EXPECT_EQ(queue.post(THIRD, 0), PostedMessageQueue::PostResult::Queued);
}

TEST(PostedMessageQueue, EmptyMessagesAreIgnored) {
  PostedMessageQueue queue;
  EXPECT_EQ(queue.post(nullptr, 0), PostedMessageQueue::PostResult::Ignored);
  EXPECT_EQ(queue.post("", 0), PostedMessageQueue::PostResult::Ignored);
  EXPECT_EQ(queue.next(0), nullptr);
}

TEST(PostedMessageQueue, TheHoldSurvivesTheMillisecondCounterWrapping) {
  PostedMessageQueue queue;
  const uint32_t nearWrap = UINT32_MAX - 100;
  queue.post(FIRST, nearWrap);
  ASSERT_EQ(queue.next(nearWrap), FIRST);
  EXPECT_EQ(queue.next(nearWrap + 200), FIRST) << "200 ms later, past the wrap";
  EXPECT_EQ(queue.next(nearWrap + HOLD), nullptr);
}
