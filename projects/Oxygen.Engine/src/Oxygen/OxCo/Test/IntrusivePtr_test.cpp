//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/OxCo/Detail/IntrusivePtr.h"

#include <gtest/gtest.h>

using oxygen::co::detail::IntrusivePtr;
using oxygen::co::detail::RefCounted;

namespace {

class TestObject : public RefCounted<TestObject> {
public:
    TestObject() = default;
    ~TestObject() { destroyed_ = true; }

    OXYGEN_DEFAULT_COPYABLE(TestObject)
    OXYGEN_DEFAULT_MOVABLE(TestObject)

    inline static bool destroyed_;
};

TEST(IntrusivePtrTest, DefaultConstructor)
{
    const IntrusivePtr<TestObject> ptr;
    EXPECT_EQ(ptr.Get(), nullptr);
    EXPECT_FALSE(ptr);
}

TEST(IntrusivePtrTest, NullptrConstructor)
{
    const IntrusivePtr<TestObject> ptr(nullptr);
    EXPECT_EQ(ptr.Get(), nullptr);
    EXPECT_FALSE(ptr);
}

TEST(IntrusivePtrTest, PointerConstructor)
{
    auto* obj = new TestObject();
    const IntrusivePtr ptr(obj);
    EXPECT_EQ(ptr.Get(), obj);
    EXPECT_TRUE(ptr);
}

TEST(IntrusivePtrTest, CopyConstructor)
{
    auto* obj = new TestObject();
    const IntrusivePtr ptr1(obj);
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) - testing
    const IntrusivePtr ptr2(ptr1);
    EXPECT_EQ(ptr1.Get(), obj);
    EXPECT_EQ(ptr2.Get(), obj);
    EXPECT_TRUE(ptr1);
    EXPECT_TRUE(ptr2);
}

TEST(IntrusivePtrTest, MoveConstructor)
{
    auto* obj = new TestObject();
    IntrusivePtr ptr1(obj);
    const IntrusivePtr ptr2(std::move(ptr1));
    EXPECT_EQ(ptr2.Get(), obj);
    EXPECT_TRUE(ptr2);
}

TEST(IntrusivePtrTest, CopyAssignment)
{
    auto* obj1 = new TestObject();
    auto* obj2 = new TestObject();
    const IntrusivePtr ptr1(obj1);
    IntrusivePtr ptr2(obj2);
    ptr2 = ptr1;
    EXPECT_EQ(ptr1.Get(), obj1);
    EXPECT_EQ(ptr2.Get(), obj1);
    EXPECT_TRUE(ptr1);
    EXPECT_TRUE(ptr2);
}

TEST(IntrusivePtrTest, MoveAssignment)
{
    auto* obj1 = new TestObject();
    auto* obj2 = new TestObject();
    IntrusivePtr ptr1(obj1);
    IntrusivePtr ptr2(obj2);
    ptr2 = std::move(ptr1);
    EXPECT_EQ(ptr2.Get(), obj1);
    EXPECT_TRUE(ptr2);
}

TEST(IntrusivePtrTest, Destructor)
{
    {
        auto* obj = new TestObject();
        IntrusivePtr ptr(obj);
    }
    EXPECT_TRUE(TestObject::destroyed_);
}

TEST(IntrusivePtrTest, ReferenceCounting)
{
    TestObject::destroyed_ = false;
    {
        auto* obj = new TestObject();
        const IntrusivePtr ptr1(obj);
        EXPECT_TRUE(ptr1);

        {
            // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) - testing
            const IntrusivePtr ptr2(ptr1);
            EXPECT_TRUE(ptr2);
            EXPECT_EQ(ptr1.Get(), ptr2.Get());

            {
                // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) - testing
                const IntrusivePtr ptr3(ptr2);
                EXPECT_TRUE(ptr3);
                EXPECT_EQ(ptr1.Get(), ptr3.Get());
            }
            EXPECT_FALSE(TestObject::destroyed_);
        }
        EXPECT_FALSE(TestObject::destroyed_);
    }
    EXPECT_TRUE(TestObject::destroyed_);
}

} // namespace
