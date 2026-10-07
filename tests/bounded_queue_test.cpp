#include <acoustic/pipeline/bounded_queue.hpp>

#include <gtest/gtest.h>

TEST(BoundedQueue, DropOldestRetainsNewestValues) {
    acoustic::BoundedQueue<int> queue{2, acoustic::BackpressurePolicy::DropOldest};
    EXPECT_EQ(queue.push(1), acoustic::PushResult::Enqueued);
    EXPECT_EQ(queue.push(2), acoustic::PushResult::Enqueued);
    EXPECT_EQ(queue.push(3), acoustic::PushResult::DroppedOldest);
    EXPECT_EQ(queue.tryPop(), 2);
    EXPECT_EQ(queue.tryPop(), 3);
}

TEST(BoundedQueue, KeepLatestCollapsesBacklog) {
    acoustic::BoundedQueue<int> queue{3, acoustic::BackpressurePolicy::KeepLatest};
    queue.push(1);
    queue.push(2);
    queue.push(3);
    EXPECT_EQ(queue.push(4), acoustic::PushResult::ReplacedWithLatest);
    EXPECT_EQ(queue.size(), 1U);
    EXPECT_EQ(queue.tryPop(), 4);
}
