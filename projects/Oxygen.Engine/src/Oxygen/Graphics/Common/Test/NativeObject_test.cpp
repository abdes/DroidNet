//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/Detail/NativeObject.h>
#include <gtest/gtest.h>
#include <stdexcept>

using oxygen::TypeId;
using oxygen::graphics::detail::NativeObject;

namespace {

struct NativeType {
    int value = 555;
};

class NativeObjectTest : public ::testing::Test {
protected:
    static constexpr uint64_t kTestInteger = 42;
    static constexpr TypeId kTestTypeId = 123;

    // Need a value with actual memory storage to be able to use its address for
    // testing.
    inline static NativeType test_value_;
};

TEST_F(NativeObjectTest, ConstructorWithInteger)
{
    const NativeObject obj(kTestInteger, kTestTypeId);
    EXPECT_EQ(obj.AsInteger(), kTestInteger);
}

TEST_F(NativeObjectTest, ConstructorWithPointer)
{
    const NativeObject obj(&test_value_, kTestTypeId);
    EXPECT_EQ(obj.AsPointer<NativeType>(), &test_value_);
}

TEST_F(NativeObjectTest, AsPointerThrowsIfNotPointer)
{
    const NativeObject obj(kTestInteger, kTestTypeId);
    EXPECT_THROW([[maybe_unused]] auto _ = obj.AsPointer<NativeType>(), std::runtime_error);
}

TEST_F(NativeObjectTest, AsPointerReturnsTheCorrectPointer)
{
    const NativeObject obj(&test_value_, kTestTypeId);
    EXPECT_EQ(obj.AsPointer<NativeType>()->value, test_value_.value);
}

TEST_F(NativeObjectTest, EqualityOperator)
{
    // Test equality for integer-based NativeObject instances
    const NativeObject integer_obj1(kTestInteger, kTestTypeId);
    const NativeObject integer_obj2(kTestInteger, kTestTypeId);
    const NativeObject different_integer_obj(99, kTestTypeId);

    EXPECT_TRUE(integer_obj1 == integer_obj2);
    EXPECT_FALSE(integer_obj1 == different_integer_obj);

    // Test equality for pointer-based NativeObject instances
    const NativeObject pointer_obj1(&test_value_, kTestTypeId);
    const NativeObject pointer_obj2(&test_value_, kTestTypeId);
    const NativeObject different_pointer_obj(reinterpret_cast<void*>(0x1ULL), kTestTypeId);

    EXPECT_TRUE(pointer_obj1 == pointer_obj2);
    EXPECT_FALSE(pointer_obj1 == different_pointer_obj);

    // Test inequality between integer-based and pointer-based NativeObject instances
    EXPECT_FALSE(integer_obj1 == pointer_obj1);
}

TEST_F(NativeObjectTest, HashFunction)
{
    // Create two NativeObject instances with the same integer and owner_type_id_
    const NativeObject obj1(kTestInteger, kTestTypeId);
    const NativeObject obj2(kTestInteger, kTestTypeId);

    // Create a NativeObject with a different integer
    const NativeObject obj3(99, kTestTypeId);

    // Create a NativeObject with a different owner_type_id_
    const NativeObject obj4(kTestInteger, kTestTypeId + 1);

    constexpr std::hash<NativeObject> hasher {};

    // Verify that the hash values are the same for identical objects
    EXPECT_EQ(hasher(obj1), hasher(obj2));

    // Verify that the hash values are different for objects with different integers
    EXPECT_NE(hasher(obj1), hasher(obj3));

    // Verify that the hash values are different for objects with different owner_type_id_
    EXPECT_NE(hasher(obj1), hasher(obj4));
}

} // namespace
