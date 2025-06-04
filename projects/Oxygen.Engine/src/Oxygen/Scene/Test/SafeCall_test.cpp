//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>
#include <optional>
#include <stdexcept>

#include <Oxygen/Scene/SafeCall.h>
#include <Oxygen/Testing/GTest.h>

namespace {

// -----------------------------------------------------------------------------
// Base test class (common properties and methods for SUTs)
// -----------------------------------------------------------------------------

struct Base {
    constexpr static int kMaxValue = 100;
    constexpr static int kBigValue = 10;
    constexpr static int kNegativeValue = -1;

    auto IncrementValue() -> bool
    {
        if (value == kMaxValue) {
            return false;
        }
        ++value;
        return true;
    }

    auto IncrementValueOrThrow() -> bool
    {
        if (value == kMaxValue) {
            throw std::runtime_error("Simulated runtime error during increment");
        }
        ++value;
        return true;
    }

    [[nodiscard]] auto HasBigValue() const -> bool
    {
        return value > kBigValue;
    }

    void ResetValue() noexcept
    {
        value = 0;
    }

    int value { 0 };
    bool is_ready { true };

    // Shared validation logic
    std::optional<std::string> Validate() const noexcept
    {
        if (!is_ready) {
            return "Component not ready";
        }
        if (value < 0 || value > kMaxValue) {
            return "Value out of range";
        }
        return std::nullopt;
    }
};

// -----------------------------------------------------------------------------
// Logging mixin
// -----------------------------------------------------------------------------

struct WithLogging {
    void LogSafeCallError(const char* reason) const noexcept
    {
        std::cerr << "Error: " << reason << std::endl;
    }
};

// -----------------------------------------------------------------------------
// CRTP-based Validators to ensure correct type is passed to SafeCall
// -----------------------------------------------------------------------------

template <typename Derived>
struct LambdaValidator : Base {
    template <typename Func>
    auto SafeCall(Func&& func) noexcept
    {
        return oxygen::SafeCall(
            *static_cast<Derived*>(this),
            [](const auto& s) { return s.Validate(); },
            std::forward<Func>(func));
    }

    template <typename Func>
    auto SafeCall(Func&& func) const noexcept
    {
        return oxygen::SafeCall(
            *static_cast<const Derived*>(this),
            [](const auto& s) { return s.Validate(); },
            std::forward<Func>(func));
    }

    [[nodiscard]] auto GetValueSafe() const noexcept -> std::optional<int>
    {
        return SafeCall([](const Derived& self) -> int {
            return self.value;
        });
    }

    auto IncrementValueSafe() noexcept -> std::optional<bool>
    {
        return SafeCall([](Derived& self) -> bool {
            return self.IncrementValue();
        });
    }

    auto IncrementValueOrThrowSafe() noexcept -> std::optional<bool>
    {
        return SafeCall([](Derived& self) -> bool {
            return self.IncrementValueOrThrow();
        });
    }

    [[nodiscard]] auto HasBigValueSafe() const noexcept -> bool
    {
        auto result = SafeCall([](const Derived& self) -> bool {
            return self.HasBigValue();
        });
        return result.value_or(false);
    }
};

template <typename Derived>
struct MemberValidator : Base {
    template <typename Func>
    auto SafeCall(Func&& func) noexcept
    {
        return oxygen::SafeCall(
            *static_cast<Derived*>(this),
            &Derived::Validate,
            std::forward<Func>(func));
    }

    template <typename Func>
    auto SafeCall(Func&& func) const noexcept
    {
        return oxygen::SafeCall(
            *static_cast<const Derived*>(this),
            &Derived::Validate,
            std::forward<Func>(func));
    }

    [[nodiscard]] auto GetValueSafe() const noexcept -> std::optional<int>
    {
        return SafeCall([](const Derived& self) -> int {
            return self.value;
        });
    }

    auto IncrementValueSafe() noexcept -> std::optional<bool>
    {
        return SafeCall([](Derived& self) -> bool {
            return self.IncrementValue();
        });
    }

    auto IncrementValueOrThrowSafe() noexcept -> std::optional<bool>
    {
        return SafeCall([](Derived& self) -> bool {
            return self.IncrementValueOrThrow();
        });
    }

    [[nodiscard]] auto HasBigValueSafe() const noexcept -> bool
    {
        auto result = SafeCall([](const Derived& self) -> bool {
            return self.HasBigValue();
        });
        return result.value_or(false);
    }
};

// Concrete types
struct LambdaValidatorNoLogging : LambdaValidator<LambdaValidatorNoLogging> { };
struct LambdaValidatorWithLogging : LambdaValidator<LambdaValidatorWithLogging>, WithLogging { };

struct MemberValidatorNoLogging : MemberValidator<MemberValidatorNoLogging> { };
struct MemberValidatorWithLogging : MemberValidator<MemberValidatorWithLogging>, WithLogging { };

// -----------------------------------------------------------------------------
// Helper for log assertion
// -----------------------------------------------------------------------------

template <typename Callable>
void ExpectLogMessage(const std::string& expected, Callable&& action)
{
    testing::internal::CaptureStderr();
    std::forward<Callable>(action)();
    std::string output = testing::internal::GetCapturedStderr();
    EXPECT_NE(output.find(expected), std::string::npos);
}

// -----------------------------------------------------------------------------
// Typed Test Fixtures for Valid and Invalid Validation Scenarios
// -----------------------------------------------------------------------------

template <typename T>
class SafeCallValidTest : public ::testing::Test {
protected:
    T sut_;
    void SetUp() override
    {
        sut_.ResetValue();
        sut_.is_ready = true;
    }
};

template <typename T>
class SafeCallInvalidTest : public ::testing::Test {
protected:
    T sut_;
    void SetUp() override
    {
        sut_.ResetValue();
        sut_.is_ready = false; // Default to not ready for invalid tests
    }
};

// Define the types to be tested
using Implementations = ::testing::Types<
    LambdaValidatorNoLogging,
    LambdaValidatorWithLogging,
    MemberValidatorNoLogging,
    MemberValidatorWithLogging>;
TYPED_TEST_SUITE(SafeCallValidTest, Implementations);
TYPED_TEST_SUITE(SafeCallInvalidTest, Implementations);

// -----------------------------------------------------------------------------
// Test Cases: Validation Passes
// ----------------------------------------------------------------------------/

TYPED_TEST(SafeCallValidTest, GetValueWhenReady)
{
    this->sut_.is_ready = true;
    this->sut_.value = 10;
    const auto result = this->sut_.GetValueSafe();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, 10);
}

TYPED_TEST(SafeCallValidTest, IncrementValueWhenReady)
{
    this->sut_.is_ready = true;
    this->sut_.value = 5;
    const auto result = this->sut_.IncrementValueSafe();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, true);
    EXPECT_EQ(this->sut_.value, 6);
}

TYPED_TEST(SafeCallValidTest, IncrementValueWhenReadyButOperationFails)
{
    this->sut_.is_ready = true;
    this->sut_.value = Base::kMaxValue;
    const auto result = this->sut_.IncrementValueSafe();
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, false);
    EXPECT_EQ(this->sut_.value, Base::kMaxValue);
}

TYPED_TEST(SafeCallValidTest, HasBigValueWhenReadyAndTrue)
{
    this->sut_.is_ready = true;
    this->sut_.value = Base::kBigValue + 1;
    const auto result = this->sut_.HasBigValueSafe();
    EXPECT_TRUE(result);
}

TYPED_TEST(SafeCallValidTest, HasBigValueWhenReadyAndFalse)
{
    this->sut_.is_ready = true;
    this->sut_.value = Base::kBigValue - 1;
    const auto result = this->sut_.HasBigValueSafe();
    EXPECT_FALSE(result);
}

TYPED_TEST(SafeCallValidTest, OperationThrowsException)
{
    this->sut_.is_ready = true;
    this->sut_.value = Base::kMaxValue;

    if constexpr (requires { this->sut_.IncrementValueOrThrowSafe(); }) {
        const auto result = this->sut_.IncrementValueOrThrowSafe();
        EXPECT_FALSE(result.has_value());

        if constexpr (oxygen::HasLogSafeCallError<TypeParam>) {
            ExpectLogMessage("Simulated runtime error during increment", [&] {
                this->sut_.is_ready = true;
                this->sut_.value = Base::kMaxValue;
                [[maybe_unused]] const auto res = this->sut_.IncrementValueOrThrowSafe();
            });
        }
    } else {
        GTEST_SKIP() << "IncrementValueOrThrowSafe not implemented for this type";
    }
}

// -----------------------------------------------------------------------------
// Test Cases: Validation Fails
// ----------------------------------------------------------------------------/

TYPED_TEST(SafeCallInvalidTest, GetValueWhenNotReady)
{
    this->sut_.is_ready = false;
    const auto result = this->sut_.GetValueSafe();
    EXPECT_FALSE(result.has_value());

    if constexpr (oxygen::HasLogSafeCallError<TypeParam>) {
        ExpectLogMessage("Error: Component not ready", [&] {
            this->sut_.is_ready = false;
            [[maybe_unused]] const auto res = this->sut_.GetValueSafe();
        });
    }
}

TYPED_TEST(SafeCallInvalidTest, IncrementValueWhenNotReady)
{
    this->sut_.is_ready = false;
    this->sut_.value = 5;
    const auto result = this->sut_.IncrementValueSafe();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(this->sut_.value, 5);

    if constexpr (oxygen::HasLogSafeCallError<TypeParam>) {
        ExpectLogMessage("Error: Component not ready", [&] {
            this->sut_.is_ready = false;
            this->sut_.value = 5;
            [[maybe_unused]] const auto res = this->sut_.IncrementValueSafe();
        });
    }
}

TYPED_TEST(SafeCallInvalidTest, HasBigValueWhenNotReady)
{
    this->sut_.is_ready = false;
    this->sut_.value = Base::kBigValue + 1;
    const auto result = this->sut_.HasBigValueSafe();
    EXPECT_FALSE(result);

    if constexpr (oxygen::HasLogSafeCallError<TypeParam>) {
        ExpectLogMessage("Error: Component not ready", [&] {
            this->sut_.is_ready = false;
            this->sut_.value = Base::kBigValue + 1;
            [[maybe_unused]] const auto res = this->sut_.HasBigValueSafe();
        });
    }
}

TYPED_TEST(SafeCallInvalidTest, GetValueWhenValueOutOfRangeNegative)
{
    this->sut_.is_ready = true;
    this->sut_.value = Base::kNegativeValue;
    const auto result = this->sut_.GetValueSafe();

    EXPECT_FALSE(result.has_value());

    if constexpr (oxygen::HasLogSafeCallError<TypeParam>) {
        ExpectLogMessage("Error: Value out of range", [&] {
            this->sut_.is_ready = true;
            this->sut_.value = Base::kNegativeValue;
            [[maybe_unused]] const auto res = this->sut_.GetValueSafe();
        });
    }
}

TYPED_TEST(SafeCallInvalidTest, GetValueWhenValueOutOfRangePositive)
{
    this->sut_.is_ready = true;
    this->sut_.value = Base::kMaxValue + 1;
    const auto result = this->sut_.GetValueSafe();

    EXPECT_FALSE(result.has_value());

    if constexpr (oxygen::HasLogSafeCallError<TypeParam>) {
        ExpectLogMessage("Error: Value out of range", [&] {
            this->sut_.is_ready = true;
            this->sut_.value = Base::kMaxValue + 1;
            [[maybe_unused]] const auto res = this->sut_.GetValueSafe();
        });
    }
}

TYPED_TEST(SafeCallInvalidTest, IncrementValueWhenValueOutOfRange)
{
    this->sut_.is_ready = true;
    this->sut_.value = Base::kMaxValue + 1;
    const auto result = this->sut_.IncrementValueSafe();

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(this->sut_.value, Base::kMaxValue + 1);

    if constexpr (oxygen::HasLogSafeCallError<TypeParam>) {
        ExpectLogMessage("Error: Value out of range", [&] {
            this->sut_.is_ready = true;
            this->sut_.value = Base::kMaxValue + 1;
            [[maybe_unused]] const auto res = this->sut_.IncrementValueSafe();
        });
    }
}

TYPED_TEST(SafeCallInvalidTest, HasBigValueWhenValueOutOfRange)
{
    this->sut_.is_ready = true;
    this->sut_.value = Base::kMaxValue + 1;
    const auto result = this->sut_.HasBigValueSafe();

    EXPECT_FALSE(result);

    if constexpr (oxygen::HasLogSafeCallError<TypeParam>) {
        ExpectLogMessage("Error: Value out of range", [&] {
            this->sut_.is_ready = true;
            this->sut_.value = Base::kMaxValue + 1;
            [[maybe_unused]] const auto res = this->sut_.HasBigValueSafe();
        });
    }
}

} // namespace
