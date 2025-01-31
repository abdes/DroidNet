//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Base/Writer.h"

#include <span>

#include <gtest/gtest.h>

#include "Mocks/MockStream.h"

using namespace oxygen::serio;
using namespace oxygen::serio::testing;
using oxygen::ByteSwap;
using oxygen::IsLittleEndian;

namespace {

class WriterTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        stream_.seek(0); // Reset position for each test
    }

    template <typename T>
    void verify_written(const T& expected)
    {
        // If type needs alignment and we're not aligned, account for padding bytes
        if constexpr (sizeof(T) > 1) {
            const size_t padding = (alignof(T) - (verify_pos_ % alignof(T))) % alignof(T);
            verify_pos_ += padding;
            ASSERT_EQ(verify_pos_ % alignof(T), 0)
                << "Data not properly aligned at position " << verify_pos_;
        }

        ASSERT_GE(stream_.get_data().size(), verify_pos_ + sizeof(T));

        T actual;
        std::memcpy(&actual, stream_.get_data().data() + verify_pos_, sizeof(T));
        if (!IsLittleEndian() && sizeof(T) > 1) {
            actual = ByteSwap(actual);
        }
        EXPECT_EQ(actual, expected);
        verify_pos_ += sizeof(T);
    }

    void verify_written_string(const std::string& expected)
    {
        // Verify length field alignment
        ASSERT_EQ(verify_pos_ % alignof(uint32_t), 0);

        uint32_t length;
        ASSERT_GE(stream_.get_data().size(), verify_pos_ + sizeof(uint32_t));
        std::memcpy(&length, stream_.get_data().data() + verify_pos_, sizeof(length));
        if (!IsLittleEndian()) {
            length = ByteSwap(length);
        }
        verify_pos_ += sizeof(uint32_t);

        ASSERT_EQ(length, expected.length());
        ASSERT_GE(stream_.get_data().size(), verify_pos_ + length);

        const std::string actual(stream_.get_data().data() + verify_pos_, length);
        EXPECT_EQ(actual, expected);
        verify_pos_ += length;

        // Verify final alignment padding
        verify_pos_ += (alignof(uint32_t) - ((sizeof(uint32_t) + length) % alignof(uint32_t))) % alignof(uint32_t);
        ASSERT_EQ(verify_pos_ % alignof(uint32_t), 0);
    }

    MockStream stream_;
    Writer<MockStream> sut_ { stream_ };
    size_t verify_pos_ { 0 };
};

TEST_F(WriterTest, WritePOD_Success)
{
    constexpr uint8_t test_byte = 0x42;
    constexpr uint32_t test_int = 0x12345678;
    constexpr float test_float = 3.14f;

    ASSERT_TRUE(sut_.write(test_byte));
    ASSERT_TRUE(sut_.write(test_int));
    ASSERT_TRUE(sut_.write(test_float));

    verify_written(test_byte);
    verify_written(test_int);
    verify_written(test_float);
}

TEST_F(WriterTest, WriteString_Success)
{
    const std::string test_str = "Hello, World!";
    ASSERT_TRUE(sut_.write_string(test_str));
    verify_written_string(test_str);
}

TEST_F(WriterTest, WriteEmptyString_Success)
{
    ASSERT_TRUE(sut_.write_string(""));
    verify_written_string("");
}

TEST_F(WriterTest, WriteString_Fails_WhenTooLarge)
{
    const std::string large_str(limits::kMaxStringLength + 1, 'x');
    const auto result = sut_.write_string(large_str);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error(), std::make_error_code(std::errc::value_too_large));
}

TEST_F(WriterTest, WriteArray_Success)
{
    const std::vector<uint32_t> test_array = { 1, 2, 3, 4, 5 };
    ASSERT_TRUE(sut_.write_array(std::span(test_array)));

    verify_written(static_cast<uint32_t>(test_array.size()));
    for (const auto& value : test_array) {
        verify_written(value);
    }
}

TEST_F(WriterTest, WriteMixedTypes_MaintainsAlignment)
{
    constexpr uint8_t byte = 0x42;
    constexpr uint32_t int_val = 0x12345678;
    const std::string str = "test";

    ASSERT_TRUE(sut_.write(byte));
    ASSERT_TRUE(sut_.write(int_val));
    ASSERT_TRUE(sut_.write_string(str));

    verify_written(byte);
    verify_written(int_val);
    verify_written_string(str);
}

TEST_F(WriterTest, WriteArray_Fails_WhenTooLarge)
{
    const std::vector<uint32_t> large_array(limits::kMaxArrayLength + 1);
    const auto result = sut_.write_array(std::span(large_array));
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error(), std::make_error_code(std::errc::message_size));
}

} // namespace
