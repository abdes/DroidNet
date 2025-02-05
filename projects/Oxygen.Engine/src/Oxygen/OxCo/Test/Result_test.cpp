//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/OxCo/Detail/Result.h"

#include <exception>
#include <string>

#include <Oxygen/Testing/GTest.h>

using oxygen::co::TaskCancelledException;
using oxygen::co::detail::Result;
using oxygen::co::detail::Storage;

namespace {

// Custom non-trivial type with move semantics
class NonTrivialType {
public:
    explicit NonTrivialType(std::string data)
        : data_(std::move(data))
    {
    }
    ~NonTrivialType() = default;
    NonTrivialType(const NonTrivialType& other) = default;
    auto operator=(const NonTrivialType& other) -> NonTrivialType& = default;
    NonTrivialType(NonTrivialType&& other) noexcept = default;
    auto operator=(NonTrivialType&& other) noexcept -> NonTrivialType& = delete;

    [[nodiscard]] auto data() const -> const std::string& { return data_; }

private:
    std::string data_;
};

// Test fixture for Storage class
template <typename T>
class StorageTest : public testing::Test {
protected:
    // Overload for lvalue references
    template <typename U = T>
    auto Value(std::remove_reference_t<U>& value) -> U
        requires std::is_lvalue_reference_v<U>
    {
        return value;
    }

    // Overload for rvalue references
    template <typename U = T>
    auto Value(std::remove_reference_t<U>&& value) -> U
        requires std::is_rvalue_reference_v<U>
    {
        return std::move(value);
    }

    // Overload for non-reference types
    template <typename U = T>
    auto Value(std::remove_reference_t<U>&& value) -> U
        requires(!std::is_reference_v<U>)
    {
        return std::forward<U>(std::move(value));
    }

    Storage<T> storage_;
};

using ValueStorageTest = StorageTest<int>;

NOLINT_TEST_F(ValueStorageTest, NoDanglingReferences)
{
    using St = Storage<int>;
    EXPECT_EQ(St::Unwrap(St::Wrap(14)), 14);
    EXPECT_EQ(St::UnwrapCRef(St::Wrap(14)), 14);

    // No dangling references
    auto value = std::make_unique<int>(14);
    auto wrapped = St::Wrap(std::move(*value)); // NOLINT(*-move-const-arg)
    value.reset();
    EXPECT_EQ(St::Unwrap(std::move(wrapped)), 14); // NOLINT(*-move-const-arg)

    // Does not compile
    // St::Unwrap(wrapped);

    // Does not compile
    // int& ref{value};
    // St::Wrap(ref);
}

// Test cases for Storage<NonTrivialType>
using RefStorageTest = StorageTest<int&>;

NOLINT_TEST_F(RefStorageTest, WrapAndUnwrapValue)
{
    using St = Storage<int&>;
    int value = 14;
    int& ref = Value(value);
    EXPECT_EQ(St::Unwrap(St::Wrap(ref)), 14);
    EXPECT_EQ(St::UnwrapCRef(St::Wrap(ref)), 14);
    // St::Wrap(14);
    // St::Wrap(std::move(14));

#if defined(OXYGEN_WITH_SANITIZER)
    int* wrapped;
    {
        int temp = 14;
        wrapped = St::Wrap(temp);
    }
    auto die = [&wrapped]() { const auto bad = St::Unwrap(wrapped); (void)bad; };
    EXPECT_DEATH(die(), "stack-use-after-scope");
#endif
}

// Test cases for Storage<NonTrivialType>
using RRefStorageTest = StorageTest<int&&>;

NOLINT_TEST_F(RRefStorageTest, WrapAndUnwrapValue)
{
    using St = Storage<int&&>;
    EXPECT_EQ(St::Unwrap(St::Wrap(Value(14))), 14);
    EXPECT_EQ(St::Unwrap(St::Wrap(14)), 14);

#if defined(OXYGEN_WITH_SANITIZER)
    auto* wrapped = St::Wrap(14);
    auto die = [&wrapped]() { const auto bad = St::Unwrap(wrapped); (void)bad; };
    EXPECT_DEATH(die(), "stack-use-after-scope");
#endif

    // Does not compile
    // int value = 14;
    // int& ref = value;
    // St::Wrap(ref);
}

// Test cases for Storage<NonTrivialType>
using NonTrivialStorageTest = StorageTest<NonTrivialType>;

NOLINT_TEST_F(NonTrivialStorageTest, WrapAndUnwrapValue)
{
    using St = Storage<NonTrivialType>;
    EXPECT_EQ(St::Unwrap(St::Wrap(NonTrivialType("Hello, World!"))).data(), "Hello, World!");
    EXPECT_EQ(St::UnwrapCRef(St::Wrap(NonTrivialType("Hello, World!"))).data(), "Hello, World!");
}

NOLINT_TEST_F(NonTrivialStorageTest, WrapAndUnwrapLValueRef)
{
    using St = Storage<NonTrivialType&>;
    NonTrivialType value("Hello, World!");
    auto* wrapped = St::Wrap(value);
    EXPECT_EQ(St::Unwrap(wrapped).data(), "Hello, World!");
    EXPECT_EQ(St::UnwrapCRef(wrapped).data(), "Hello, World!");
}

NOLINT_TEST_F(NonTrivialStorageTest, WrapAndUnwrapRValueRef)
{
    using St = Storage<NonTrivialType&&>;
    NonTrivialType value("Hello, World!");
    auto* wrapped = St::Wrap(std::move(value));
    EXPECT_EQ(St::Unwrap(wrapped).data(), "Hello, World!");
    EXPECT_EQ(St::UnwrapCRef(wrapped).data(), "Hello, World!");
}

NOLINT_TEST_F(NonTrivialStorageTest, WrapAndUnwrapConstRef)
{
    using St = Storage<const NonTrivialType&>;
    const NonTrivialType value("Hello, World!");
    const auto* wrapped = St::Wrap(value);
    EXPECT_EQ(St::Unwrap(wrapped).data(), "Hello, World!");
    EXPECT_EQ(St::UnwrapCRef(wrapped).data(), "Hello, World!");
}

// Test fixture for Result class
template <typename T>
class ResultTest : public testing::Test {
protected:
    Result<T> result_;
};

// Test cases for Result<NonTrivialType>
using NonTrivialResultTest = ResultTest<NonTrivialType>;

NOLINT_TEST_F(NonTrivialResultTest, StoreValue)
{
    result_.StoreValue(NonTrivialType("Hello, World!"));
    EXPECT_FALSE(result_.WasCancelled());
    EXPECT_TRUE(result_.Completed());
    EXPECT_TRUE(result_.HasValue());
    EXPECT_FALSE(result_.HasException());
    EXPECT_EQ(std::move(result_).Value().data(), "Hello, World!");
}

NOLINT_TEST_F(NonTrivialResultTest, StoreException)
{
    try {
        throw std::runtime_error("Test exception");
    } catch (...) {
        result_.StoreException(std::current_exception());
    }
    EXPECT_FALSE(result_.WasCancelled());
    EXPECT_TRUE(result_.Completed());
    EXPECT_FALSE(result_.HasValue());
    EXPECT_TRUE(result_.HasException());
    NOLINT_EXPECT_THROW(std::move(result_).Value(), std::runtime_error);
}

NOLINT_TEST_F(NonTrivialResultTest, MarkCancelled)
{
    result_.MarkCancelled();
    EXPECT_TRUE(result_.WasCancelled());
    EXPECT_TRUE(result_.Completed());
    EXPECT_FALSE(result_.HasValue());
    EXPECT_FALSE(result_.HasException());
}

NOLINT_TEST_F(NonTrivialResultTest, ValueOfCancelledTaskThrows)
{
    result_.MarkCancelled();
    NOLINT_EXPECT_THROW(std::move(result_).Value(), TaskCancelledException);
}

NOLINT_TEST_F(NonTrivialResultTest, StoreAndRetrieveLValueRef)
{
    NonTrivialType lvalue("Lvalue");
    result_.StoreValue(std::move(lvalue));
    EXPECT_TRUE(result_.HasValue());
    EXPECT_EQ(std::move(result_).Value().data(), "Lvalue");
}

NOLINT_TEST_F(NonTrivialResultTest, StoreAndRetrieveRValueRef)
{
    result_.StoreValue(NonTrivialType("Rvalue"));
    EXPECT_TRUE(result_.HasValue());
    EXPECT_EQ(std::move(result_).Value().data(), "Rvalue");
}

NOLINT_TEST_F(NonTrivialResultTest, StoreAndRetrieveConstRef)
{
    const NonTrivialType const_value("ConstRef");
    result_.StoreValue(const_value);
    EXPECT_TRUE(result_.HasValue());
    EXPECT_EQ(std::move(result_).Value().data(), "ConstRef");
}

// Test cases for Result<void>
using VoidResultTest = ResultTest<void>;

NOLINT_TEST_F(VoidResultTest, StoreSuccess)
{
    result_.StoreSuccess();
    EXPECT_FALSE(result_.WasCancelled());
    EXPECT_TRUE(result_.Completed());
    EXPECT_TRUE(result_.HasValue());
    EXPECT_FALSE(result_.HasException());
    NOLINT_EXPECT_NO_THROW(std::move(result_).Value());
}

NOLINT_TEST_F(VoidResultTest, StoreExceptionAndCheckValue)
{
    try {
        throw std::runtime_error("Test exception");
    } catch (...) {
        result_.StoreException(std::current_exception());
    }
    EXPECT_FALSE(result_.WasCancelled());
    EXPECT_TRUE(result_.Completed());
    EXPECT_FALSE(result_.HasValue());
    EXPECT_TRUE(result_.HasException());
    NOLINT_EXPECT_THROW(std::move(result_).Value(), std::runtime_error);
}

NOLINT_TEST_F(VoidResultTest, MarkCancelled)
{
    result_.MarkCancelled();
    EXPECT_TRUE(result_.WasCancelled());
    EXPECT_TRUE(result_.Completed());
    EXPECT_FALSE(result_.HasValue());
    EXPECT_FALSE(result_.HasException());
}

NOLINT_TEST_F(VoidResultTest, ValueOfCancelledTaskThrows)
{
    result_.MarkCancelled();
    NOLINT_EXPECT_THROW(std::move(result_).Value(), TaskCancelledException);
}

using PointerResultTest = ResultTest<std::unique_ptr<int>>;

NOLINT_TEST_F(PointerResultTest, StoreValue)
{
    auto value = std::make_unique<int>(42);
    result_.StoreValue(std::move(value));
    EXPECT_EQ(*std::move(result_).Value(), 42);
}

} // namespace
