#include <audition/pipeline/bounded_queue.hpp>

#include <gtest/gtest.h>

TEST(BoundedQueue, DropOldestRetainsNewestValues) {
    audition::BoundedQueue<int> queue{2, audition::BackpressurePolicy::DropOldest};
    EXPECT_EQ(queue.push(1), audition::PushResult::Enqueued);
    EXPECT_EQ(queue.push(2), audition::PushResult::Enqueued);
    EXPECT_EQ(queue.push(3), audition::PushResult::DroppedOldest);
    EXPECT_EQ(queue.tryPop(), 2);
    EXPECT_EQ(queue.tryPop(), 3);
}

TEST(BoundedQueue, KeepLatestCollapsesBacklog) {
    audition::BoundedQueue<int> queue{3, audition::BackpressurePolicy::KeepLatest};
    queue.push(1);
    queue.push(2);
    queue.push(3);
    EXPECT_EQ(queue.push(4), audition::PushResult::ReplacedWithLatest);
    EXPECT_EQ(queue.size(), 1U);
    EXPECT_EQ(queue.tryPop(), 4);
}
