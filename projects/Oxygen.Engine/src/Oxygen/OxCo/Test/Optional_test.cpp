//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/OxCo/Detail/Optional.h"

#include <gtest/gtest.h>

using namespace oxygen::co;

template <typename T>
class OptionalTest : public ::testing::Test {
protected:
    T value_ = 42;
    T default_value_ = 0;
    T another_value_ = 100;
};

template <>
class OptionalTest<int&> : public ::testing::Test {
protected:
    int value_ = 42;
    int default_value_ = 0;
    int another_value_ = 100;
};

using TestTypes = ::testing::Types<int, int&>;

class TestTypeNames {
public:
    template <typename T>
    static auto GetName(int) -> std::string
    {
        if (std::is_same_v<T, int>) {
            return "int";
        }
        if (std::is_same_v<T, int&>) {
            return "int&";
        }
        return "Unknown";
    }
};

TYPED_TEST_SUITE(OptionalTest, TestTypes, TestTypeNames);

TYPED_TEST(OptionalTest, DefaultConstructor)
{
    Optional<TypeParam> opt;
    EXPECT_FALSE(opt.has_value());
    EXPECT_FALSE(static_cast<bool>(opt));
}

TYPED_TEST(OptionalTest, ValueConstructor)
{
    Optional<TypeParam> opt(this->value_);
    EXPECT_TRUE(opt.has_value());
    EXPECT_TRUE(static_cast<bool>(opt));
    EXPECT_EQ(*opt, this->value_);
    EXPECT_EQ(opt.value(), this->value_);
}

TYPED_TEST(OptionalTest, NulloptConstructor)
{
    Optional<TypeParam> opt(std::nullopt);
    EXPECT_FALSE(opt.has_value());
    EXPECT_FALSE(static_cast<bool>(opt));
}

TYPED_TEST(OptionalTest, ValueOr)
{
    Optional<TypeParam> opt;
    EXPECT_EQ(opt.value_or(this->default_value_), this->default_value_);

    opt = Optional<TypeParam>(this->value_);
    EXPECT_EQ(opt.value_or(this->default_value_), this->value_);
}

TYPED_TEST(OptionalTest, Reset)
{
    Optional<TypeParam> opt(this->value_);
    EXPECT_TRUE(opt.has_value());

    opt.reset();
    EXPECT_FALSE(opt.has_value());
}

TYPED_TEST(OptionalTest, Swap)
{
    Optional<TypeParam> opt1(this->value_);
    Optional<TypeParam> opt2(this->another_value_);

    opt1.swap(opt2);
    EXPECT_EQ(*opt1, this->another_value_);
    EXPECT_EQ(*opt2, this->value_);
}

TYPED_TEST(OptionalTest, DereferenceOperators)
{
    Optional<TypeParam> opt(this->value_);
    EXPECT_EQ(*opt, this->value_);
    if constexpr (std::is_reference_v<TypeParam>) {
        EXPECT_EQ(opt.operator->(), &this->value_);
    } else {
        EXPECT_EQ(opt.operator->(), &opt.value());
    }
}

TYPED_TEST(OptionalTest, ConstDereferenceOperators)
{
    const Optional<TypeParam> opt(this->value_);
    EXPECT_EQ(*opt, this->value_);
    if constexpr (std::is_reference_v<TypeParam>) {
        EXPECT_EQ(opt.operator->(), &this->value_);
    } else {
        EXPECT_EQ(opt.operator->(), &opt.value());
    }
}

TYPED_TEST(OptionalTest, ValueThrowsOnNullopt)
{
    Optional<TypeParam> opt;
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) for testing purposes
    EXPECT_THROW([[maybe_unused]] auto _ = opt.value(), std::bad_optional_access);
}
