//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Graphics/Common/ShaderByteCode.h"

#include <array>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

using namespace oxygen::graphics;

// Test classes
namespace {

// Base class for buffers
struct BasicBuffer {
    [[maybe_unused]] BasicBuffer()
        : size(0)
        , data(nullptr)
    {
    }
    BasicBuffer(const size_t buffer_size, const uint32_t* buffer_data)
        : size(buffer_size)
        , data(new uint32_t[buffer_size])
    {
        std::memcpy(data, buffer_data, buffer_size * sizeof(uint32_t));
    }

    BasicBuffer(const BasicBuffer& other) = delete;
    BasicBuffer& operator=(const BasicBuffer& other) = delete;

    BasicBuffer(BasicBuffer&& other) noexcept
        : size(other.size)
        , data(other.data)
    {
        other.size = 0;
        other.data = nullptr;
    }

    [[maybe_unused]] auto operator=(BasicBuffer&& other) noexcept
        -> BasicBuffer&
    {
        if (this != &other) {
            delete[] data;
            size = other.size;
            data = other.data;
            other.size = 0;
            other.data = nullptr;
        }
        return *this;
    }

    virtual ~BasicBuffer()
    {
        // Do nothing - expects the Deleter to be called
        EXPECT_EQ(data, nullptr);
    }

    size_t size;
    uint32_t* data;
};

// Define a deleter function for BasicBuffer
void BasicBufferDeleter(const uint32_t* data) { delete[] data; }

// MockBuffer class for testing HasBufferMethods concept
class ResourceBuffer final : BasicBuffer {
public:
    using BasicBuffer::BasicBuffer;
    ResourceBuffer(const size_t buffer_size, const uint32_t* buffer_data,
        bool* released = nullptr)
        : BasicBuffer(buffer_size, buffer_data)
        , released_(released)
    {
    }

    [[nodiscard]] [[maybe_unused]] auto GetBufferSize() const noexcept -> size_t
    {
        return size;
    }
    [[nodiscard]] [[maybe_unused]] auto GetBufferPointer() const noexcept
        -> void*
    {
        return data;
    }
    [[maybe_unused]] void Release() noexcept
    {
        if (data == nullptr)
            return;

        delete[] data;
        data = nullptr;
        size = 0;
        if (released_) {
            *released_ = true;
        }
    }

private:
    bool* released_ { nullptr };
};

// MockBuffer class for testing HasBufferMethods concept
class ManagedResourceBuffer final {
public:
    ManagedResourceBuffer(const size_t buffer_size, const uint32_t* buffer_data,
        bool* released = nullptr)
        : buffer_ { new ResourceBuffer(buffer_size, buffer_data, released) }
    {
    }

    ~ManagedResourceBuffer() noexcept
    {
        if (buffer_ == nullptr)
            return;

        buffer_->Release();
        delete buffer_;
    }

    ManagedResourceBuffer(const ManagedResourceBuffer& other) = delete;
    ManagedResourceBuffer& operator=(const ManagedResourceBuffer& other) = delete;

    ManagedResourceBuffer(ManagedResourceBuffer&& other) noexcept
        : buffer_(other.buffer_)
    {
        other.buffer_ = nullptr;
    }
    [[maybe_unused]] auto operator=(ManagedResourceBuffer&& other) noexcept
        -> ManagedResourceBuffer&
    {
        if (this != &other) {
            buffer_->Release();
            delete buffer_;
            buffer_ = other.buffer_;
            other.buffer_ = nullptr;
        }
        return *this;
    }

    ResourceBuffer* operator->() const noexcept { return buffer_; }
    operator bool() const noexcept { return buffer_ != nullptr; }

private:
    ResourceBuffer* buffer_ { nullptr };
};
} // namespace

// Helper functions to get test values for different buffer types
namespace {

template <IsContiguousContainer BufferType>
auto GetTestValue() -> std::tuple<BufferType, size_t, const uint32_t*>
{
    BufferType buffer = { 1, 2, 3, 4 };
    return std::make_tuple(
        std::move(buffer),
        buffer.size() * sizeof(typename BufferType::value_type),
        buffer.data());
}

// Helper function to initialize buffer for custom struct with size and data
// members
template <BasicBufferWithOwnershipTransfer BufferType>
auto GetTestValue() -> std::tuple<BufferType, size_t, const uint32_t*>
{
    static constexpr uint32_t default_data[4] { 1, 2, 3, 4 };
    BasicBuffer buffer { sizeof(default_data) / sizeof(uint32_t), default_data };
    return std::make_tuple(std::move(buffer), buffer.size, buffer.data);
}

// Helper function to initialize buffer for types with GetBufferPointer,
// GetBufferSize, and Release methods
template <ManagedBuffer BufferType>
auto GetTestValue() -> std::tuple<BufferType, size_t, const uint32_t*>
{
    static constexpr uint32_t default_data[4] { 1, 2, 3, 4 };
    ManagedResourceBuffer buffer(sizeof(default_data) / sizeof(uint32_t),
        default_data);
    return std::make_tuple(std::move(buffer), buffer->GetBufferSize(),
        static_cast<uint32_t*>(buffer->GetBufferPointer()));
}
} // namespace

// Baseline tests for ShaderByteCode
namespace {

template <typename T>
class ShaderByteCodeTest : public ::testing::Test {
public:
    using BufferType = T;

    [[nodiscard]] auto CreateShaderByteCode() -> std::pair<ShaderByteCode<BufferType>, size_t>
    {
        auto [buffer, size, data] = GetTestValue<BufferType>();
        if constexpr (std::is_same_v<BufferType, BasicBuffer>) {
            return { ShaderByteCode<BufferType>(std::move(buffer), BasicBufferDeleter), size };
        } else {
            return { ShaderByteCode<BufferType>(std::move(buffer)), size };
        }
    }

    [[nodiscard]] auto GetOriginalBuffer()
        -> std::tuple<BufferType, size_t, const uint32_t*>
    {
        return GetTestValue<BufferType>();
    }
};

using TestTypes = ::testing::Types<
    std::vector<uint32_t>,
    std::array<uint32_t, 4>,
    BasicBuffer,
    ManagedResourceBuffer>;

TYPED_TEST_SUITE(ShaderByteCodeTest, TestTypes);

// Test instantiation of ShaderByteCode for all supported types
TYPED_TEST(ShaderByteCodeTest, InstantiationTest)
{
    auto [shader_byte_code, size] = this->CreateShaderByteCode();
    EXPECT_TRUE(shader_byte_code.Size() > 0);
}

// Test basic accessors for Size() and Data()
TYPED_TEST(ShaderByteCodeTest, AccessorsTest)
{
    auto [shader_byte_code, size] = this->CreateShaderByteCode();
    EXPECT_EQ(shader_byte_code.Size(), size);
    EXPECT_NE(shader_byte_code.Data(), nullptr);
}

TEST(ShaderByteCodeTest, BasicBufferDeleterGetsCalled)
{
    uint32_t data[] = { 1, 2, 3, 4 };
    BasicBuffer buffer { 4, data };
    bool deleter_called = false;
    {
        auto deleter = [&deleter_called](const uint32_t* /*data*/) {
            deleter_called = true;
        };
        ShaderByteCode<BasicBuffer> shader_byte_code(std::move(buffer), deleter);
    }
    EXPECT_TRUE(deleter_called);
}

TEST(ShaderByteCodeTest, ManagedBufferReleaseGetsCalled)
{
    uint32_t data[] = { 1, 2, 3, 4 };
    bool released = false;

    {
        static_assert(ManagedBuffer<ManagedResourceBuffer>);

        ManagedResourceBuffer buffer(4, data, &released);
        ShaderByteCode<ManagedResourceBuffer> shader_byte_code(std::move(buffer));
    }
    EXPECT_TRUE(released);
}

// We should handle empty buffers fine
TEST(ShaderByteCodeTest, NullDataPointer)
{
    BasicBuffer buffer {};
    EXPECT_NO_THROW({
        ShaderByteCode<BasicBuffer> shaderByteCode(std::move(buffer),
            BasicBufferDeleter);
    });
}
} // namespace

// Move semantics tests for ShaderByteCode
namespace {

template <typename T>
class ShaderByteCodeMoveTest : public ShaderByteCodeTest<T> {
};

using MoveTestTypes = ::testing::Types<std::vector<uint32_t>, BasicBuffer, ManagedResourceBuffer>;

TYPED_TEST_SUITE(ShaderByteCodeMoveTest, MoveTestTypes, );

TYPED_TEST(ShaderByteCodeMoveTest, OriginalBufferIsMoved)
{
    auto [original_buffer, original_size, original_data] = this->GetOriginalBuffer();

    ShaderByteCode<typename TestFixture::BufferType> shader_byte_code(
        std::move(original_buffer));

    EXPECT_EQ(shader_byte_code.Size(), original_size);
    EXPECT_EQ(shader_byte_code.Data(), original_data);
}
} // namespace

// Test suite for ShaderByteCode with std::vector<uint32_t> using various values
namespace {

class VectorBufferTest
    : public ::testing::TestWithParam<std::vector<uint32_t>> {
};

INSTANTIATE_TEST_SUITE_P(ShaderByteCodeValueTests, VectorBufferTest,
    ::testing::Values(std::vector<uint32_t> {},
        std::vector<uint32_t> { 1 },
        std::vector<uint32_t> { 1, 2, 3, 4 }));

TEST_P(VectorBufferTest, SizeTest)
{
    auto buffer = GetParam();
    const size_t original_size = buffer.size();
    const ShaderByteCode<std::vector<uint32_t>> shader_byte_code(
        std::move(buffer));
    EXPECT_EQ(shader_byte_code.Size(), original_size * sizeof(uint32_t));
}

TEST_P(VectorBufferTest, DataTest)
{
    auto buffer = GetParam();
    const uint32_t* original_data = buffer.data();
    const ShaderByteCode<std::vector<uint32_t>> shader_byte_code(
        std::move(buffer));
    EXPECT_EQ(shader_byte_code.Data(), original_data);
}
} // namespace

// Type-parameterized test suite to verify that ShaderByteCode rejects
// unsupported types
namespace {

template <typename T>
class ShaderByteCodeUnsupportedTest : public ::testing::Test {
};

using UnsupportedTypes = ::testing::Types<int, float, std::string>;

TYPED_TEST_SUITE(ShaderByteCodeUnsupportedTest, UnsupportedTypes);

TYPED_TEST(ShaderByteCodeUnsupportedTest, RejectsUnsupportedTypes)
{
    using BufferType = TypeParam;
    EXPECT_FALSE((IsContiguousContainer<BufferType>));
    EXPECT_FALSE((ManagedBuffer<BufferType>));
    EXPECT_FALSE((BasicBufferWithOwnershipTransfer<BufferType>));
}

} // namespace
