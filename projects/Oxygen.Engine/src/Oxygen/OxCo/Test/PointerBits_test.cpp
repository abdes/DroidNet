//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/OxCo/Detail/PointerBits.h"
#include <gtest/gtest.h>

using namespace oxygen::co::detail;

namespace {

TEST(PointerBitsTest, DefaultConstructor)
{
    constexpr PointerBits<int, uint8_t, 2> pb;
    EXPECT_EQ(pb.Ptr(), nullptr);
    EXPECT_EQ(pb.Bits(), 0);
}

TEST(PointerBitsTest, ParameterizedConstructor)
{
    int value = 42;
    const PointerBits<int, uint8_t, 2> pb(&value, 3);
    EXPECT_EQ(pb.Ptr(), &value);
    EXPECT_EQ(pb.Bits(), 3);
}

TEST(PointerBitsTest, SetMethod)
{
    int value1 = 42;
    int value2 = 84;
    PointerBits<int, uint8_t, 2> pb(&value1, 1);
    pb.Set(&value2, 2);
    EXPECT_EQ(pb.Ptr(), &value2);
    EXPECT_EQ(pb.Bits(), 2);
}

TEST(PointerBitsTest, BitsMaskCheck)
{
    int value = 42;
    PointerBits<int, uint8_t, 2> pb(&value, 3);
    EXPECT_EQ(pb.Bits(), 3);
    pb.Set(&value, 0);
    EXPECT_EQ(pb.Bits(), 0);
}

TEST(PointerBitsTest, AlignmentCheck)
{
    struct alignas(4) AlignedStruct {
        int data;
    };

    AlignedStruct value {};
    const PointerBits<AlignedStruct, uint8_t, 2> pb(&value, 3);
    EXPECT_EQ(pb.Ptr(), &value);
    EXPECT_EQ(pb.Bits(), 3);
}

TEST(PointerBitsTest, InvalidBits)
{
    int value = 42;
    PointerBits<int, uint8_t, 2> pb;
    EXPECT_DEATH(pb.Set(&value, 4), ".*"); // 4 is out of range for 2-bit width
}

TEST(PointerBitsTest, NullPointer)
{
    const PointerBits<int, uint8_t, 2> pb(nullptr, 3);
    EXPECT_EQ(pb.Ptr(), nullptr);
    EXPECT_EQ(pb.Bits(), 3);
}

TEST(PointerBitsTest, LargeWidth)
{
    int value = 42;
    const PointerBits<int, uint16_t, 16> pb(&value, 65535);
    EXPECT_EQ(pb.Ptr(), &value);
    EXPECT_EQ(pb.Bits(), 65535);
}

} // namespace
