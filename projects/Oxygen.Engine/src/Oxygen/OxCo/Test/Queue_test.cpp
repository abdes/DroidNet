//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/OxCo/Detail/Queue.h"
#include <gtest/gtest.h>

using namespace oxygen::co::detail;

namespace {

TEST(QueueTest, ConstructWithInitialCapacity)
{
    const Queue<int> q(10);
    EXPECT_EQ(q.Capacity(), 10);
    EXPECT_EQ(q.Size(), 0);
    EXPECT_TRUE(q.Empty());
}

TEST(QueueTest, MoveConstructor)
{
    Queue<int> q1(10);
    q1.PushBack(1);
    auto q2(std::move(q1));
    EXPECT_EQ(q2.Size(), 1);
    EXPECT_EQ(q2.Front(), 1);
}

TEST(QueueTest, MoveAssignment)
{
    Queue<int> q1(10);
    q1.PushBack(1);
    Queue<int> q2(5);
    q2 = std::move(q1);
    EXPECT_EQ(q2.Size(), 1);
    EXPECT_EQ(q2.Front(), 1);
}

TEST(QueueTest, PushBackSingleElement)
{
    Queue<int> q(10);
    q.PushBack(1);
    EXPECT_EQ(q.Size(), 1);
    EXPECT_EQ(q.Front(), 1);
}

TEST(QueueTest, PushBackMultipleElements)
{
    Queue<int> q(10);
    q.PushBack(1);
    q.PushBack(2);
    EXPECT_EQ(q.Size(), 2);
    EXPECT_EQ(q.Front(), 1);
}

TEST(QueueTest, PopFrontSingleElement)
{
    Queue<int> q(10);
    q.PushBack(1);
    q.PopFront();
    EXPECT_EQ(q.Size(), 0);
    EXPECT_TRUE(q.Empty());
}

TEST(QueueTest, PopFrontMultipleElements)
{
    Queue<int> q(10);
    q.PushBack(1);
    q.PushBack(2);
    q.PopFront();
    EXPECT_EQ(q.Size(), 1);
    EXPECT_EQ(q.Front(), 2);
}

TEST(QueueTest, CapacityCheck)
{
    const Queue<int> q(10);
    EXPECT_EQ(q.Capacity(), 10);
}

TEST(QueueTest, SizeCheck)
{
    Queue<int> q(10);
    q.PushBack(1);
    EXPECT_EQ(q.Size(), 1);
}

TEST(QueueTest, EmptyCheck)
{
    Queue<int> q(10);
    EXPECT_TRUE(q.Empty());
    q.PushBack(1);
    EXPECT_FALSE(q.Empty());
}

TEST(QueueTest, PushBackUntilGrow)
{
    Queue<int> q(2);
    q.PushBack(1);
    q.PushBack(2);
    q.PushBack(3); // Should trigger grow
    EXPECT_EQ(q.Capacity(), 4);
    EXPECT_EQ(q.Size(), 3);
}

TEST(QueueTest, PushBackAndPopFrontWrapAround)
{
    Queue<int> q(3);
    q.PushBack(1);
    q.PushBack(2);
    q.PopFront();
    q.PushBack(3);
    q.PushBack(4); // Should wrap around
    EXPECT_EQ(q.Size(), 3);
    EXPECT_EQ(q.Front(), 2);
}

TEST(QueueTest, Destructor)
{
    Queue<int> q(10);
    q.PushBack(1);
    q.PushBack(2);
    // Destructor will be called at the end of the scope
}

TEST(QueueTest, PopFrontFromEmptyQueue)
{
    Queue<int> q(10);
    EXPECT_NO_THROW(q.PopFront());
}

TEST(QueueTest, EmplaceBackSingleElement)
{
    Queue<std::pair<int, int>> q(10);
    q.EmplaceBack(1, 2);
    EXPECT_EQ(q.Size(), 1);
    EXPECT_EQ(q.Front().first, 1);
    EXPECT_EQ(q.Front().second, 2);
}

TEST(QueueTest, EmplaceBackMultipleElements)
{
    Queue<std::pair<int, int>> q(10);
    q.EmplaceBack(1, 2);
    q.EmplaceBack(3, 4);
    EXPECT_EQ(q.Size(), 2);
    EXPECT_EQ(q.Front().first, 1);
    EXPECT_EQ(q.Front().second, 2);
    q.PopFront();
    EXPECT_EQ(q.Front().first, 3);
    EXPECT_EQ(q.Front().second, 4);
}

TEST(QueueTest, EmplaceBackUntilGrow)
{
    Queue<std::pair<int, int>> q(2);
    q.EmplaceBack(1, 2);
    q.EmplaceBack(3, 4);
    q.EmplaceBack(5, 6); // Should trigger grow
    EXPECT_EQ(q.Capacity(), 4);
    EXPECT_EQ(q.Size(), 3);
    EXPECT_EQ(q.Front().first, 1);
    EXPECT_EQ(q.Front().second, 2);
}

struct Item {
    inline static int destruction_count { 0 };
    Item() = default;
    ~Item() { ++destruction_count; }
    OXYGEN_DEFAULT_COPYABLE(Item)
    OXYGEN_DEFAULT_MOVABLE(Item)
};

TEST(QueueTest, PopFrontCallsDestructor)
{
    EXPECT_EQ(0, Item::destruction_count);
    {
        Item t;
    }
    EXPECT_EQ(1, Item::destruction_count);
    {
        Queue<Item> q(1);
        q.EmplaceBack();
        EXPECT_EQ(1, Item::destruction_count);
        q.PopFront();
        EXPECT_EQ(2, Item::destruction_count);

        q.EmplaceBack();
    }
    EXPECT_EQ(3, Item::destruction_count);
}

} // namespace
