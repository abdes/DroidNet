//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Writer.h>

#include <span>

#include <Oxygen/Testing/GTest.h>

#include "Mocks/MockStream.h"

using oxygen::ByteSwap;
using oxygen::IsLittleEndian;
using oxygen::serio::Writer;
using oxygen::serio::testing::MockStream;

namespace {

class WriterTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    stream_.seek(0); // Reset position for each test
  }

  template <typename T> auto verify_written(const T& expected) -> void
  {
    // If type needs alignment and we're not aligned, account for padding bytes
    if constexpr (sizeof(T) > 1) {
      const size_t padding
        = (alignof(T) - (verify_pos_ % alignof(T))) % alignof(T);
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

  auto verify_written_string(const std::string& expected) -> void
  {
    // Verify length field alignment
    ASSERT_EQ(verify_pos_ % alignof(uint32_t), 0);

    uint32_t length;
    ASSERT_GE(stream_.get_data().size(), verify_pos_ + sizeof(uint32_t));
    std::memcpy(
      &length, stream_.get_data().data() + verify_pos_, sizeof(length));
    if (!IsLittleEndian()) {
      length = ByteSwap(length);
    }
    verify_pos_ += sizeof(uint32_t);

    ASSERT_EQ(length, expected.length());
    ASSERT_GE(stream_.get_data().size(), verify_pos_ + length);

    const std::string actual(
      reinterpret_cast<const char*>(stream_.get_data().data()) + verify_pos_,
      length);
    EXPECT_EQ(actual, expected);
    verify_pos_ += length;

    // Verify final alignment padding
    verify_pos_
      += (alignof(uint32_t) - ((sizeof(uint32_t) + length) % alignof(uint32_t)))
      % alignof(uint32_t);
    ASSERT_EQ(verify_pos_ % alignof(uint32_t), 0);
  }

  MockStream stream_;
  Writer<MockStream> sut_ { stream_ };
  size_t verify_pos_ { 0 };
};

NOLINT_TEST_F(WriterTest, WritePOD_Success)
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

NOLINT_TEST_F(WriterTest, WriteString_Success)
{
  const std::string test_str = "Hello, World!";
  ASSERT_TRUE(sut_.write_string(test_str));
  verify_written_string(test_str);
}

NOLINT_TEST_F(WriterTest, WriteEmptyString_Success)
{
  ASSERT_TRUE(sut_.write_string(""));
  verify_written_string("");
}

NOLINT_TEST_F(WriterTest, WriteString_Fails_WhenTooLarge)
{
  const std::string large_str(oxygen::serio::limits::kMaxStringLength + 1, 'x');
  const auto result = sut_.write_string(large_str);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::value_too_large));
}

NOLINT_TEST_F(WriterTest, WriteArray_Success)
{
  const std::vector<uint32_t> test_array = { 1, 2, 3, 4, 5 };
  ASSERT_TRUE(sut_.write_array(std::span(test_array)));

  verify_written(static_cast<uint32_t>(test_array.size()));
  for (const auto& value : test_array) {
    verify_written(value);
  }
}

NOLINT_TEST_F(WriterTest, WriteMixedTypes_MaintainsAlignment)
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

NOLINT_TEST_F(WriterTest, WriteArray_Fails_WhenTooLarge)
{
  const std::vector<uint32_t> large_array(
    oxygen::serio::limits::kMaxArrayLength + 1);
  const auto result = sut_.write_array(std::span(large_array));
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::message_size));
}

NOLINT_TEST_F(WriterTest, WriteBlob_Success)
{
  const std::vector<std::byte> test_data = { 'a'_b, 'b'_b, 'c'_b, 'd'_b };
  ASSERT_TRUE(sut_.write_blob(test_data));
  // Verify written bytes
  ASSERT_GE(stream_.get_data().size(), test_data.size());
  EXPECT_TRUE(
    std::equal(test_data.begin(), test_data.end(), stream_.get_data().begin()));
}

NOLINT_TEST_F(WriterTest, WriteBlob_Empty)
{
  const std::vector<std::byte> empty_data;
  ASSERT_TRUE(sut_.write_blob(empty_data));
  // No bytes should be written
  EXPECT_TRUE(stream_.get_data().empty());
}

NOLINT_TEST_F(WriterTest, WriteBlob_Fails_OnStreamError)
{
  const std::vector<std::byte> test_data = { 'x'_b, 'y'_b };
  stream_.force_fail(true);
  const auto result = sut_.write_blob(test_data);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

//=== Scoped Alignment Guard Integration Tests ===----------------------------//

//! Tests that Writer writes values correctly with explicit scoped alignment
//! guard.
/*! Covers auto alignment, specific alignment, nested scopes, and edge cases.
  @see oxygen::serio::AlignmentGuard, oxygen::serio::Writer
*/

class WriterAlignmentGuardIntegrationTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    stream_.seek(0);
    verify_pos_ = 0;
  }

  MockStream stream_;
  Writer<MockStream> sut_ { stream_ };
  size_t verify_pos_ { 0 };

  template <typename T> auto verify_written(const T& expected) -> void
  {
    if constexpr (sizeof(T) > 1) {
      const size_t padding
        = (alignof(T) - (verify_pos_ % alignof(T))) % alignof(T);
      verify_pos_ += padding;
      ASSERT_EQ(verify_pos_ % alignof(T), 0);
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
};

NOLINT_TEST_F(
  WriterAlignmentGuardIntegrationTest, WritesValueWithExplicitAlignment)
{
  constexpr uint32_t test_value = 0xCAFEBABE;
  constexpr size_t alignment = 16;
  {
    auto guard = sut_.ScopedAlignment(alignment);
    ASSERT_TRUE(sut_.write(test_value));
  }
  verify_written(test_value);
}

NOLINT_TEST_F(WriterAlignmentGuardIntegrationTest, WritesValueWithAutoAlignment)
{
  constexpr uint32_t test_value = 0xAABBCCDD;
  {
    auto guard = sut_.ScopedAlignment(0); // auto-align
    ASSERT_TRUE(sut_.write(test_value));
  }
  verify_written(test_value);
}

NOLINT_TEST_F(
  WriterAlignmentGuardIntegrationTest, WritesValuesWithNestedAlignmentScopes)
{
  constexpr uint32_t value1 = 0x11111111;
  constexpr uint64_t value2 = 0x2222222233333333ULL;
  constexpr uint32_t value3 = 0x44444444;

  {
    auto guard4 = sut_.ScopedAlignment(4);
    ASSERT_TRUE(sut_.write(value1));
    {
      auto guard8 = sut_.ScopedAlignment(8);
      ASSERT_TRUE(sut_.write(value2));
    }
    ASSERT_TRUE(sut_.write(value3));
  }
  verify_written(value1);
  verify_written(value2);
  verify_written(value3);
}

NOLINT_TEST_F(WriterAlignmentGuardIntegrationTest, ThrowsOnInvalidAlignment)
{
  EXPECT_THROW((void)sut_.ScopedAlignment(3), std::invalid_argument);
  EXPECT_NO_THROW((void)sut_.ScopedAlignment(0));
  EXPECT_NO_THROW((void)sut_.ScopedAlignment(static_cast<uint8_t>(256)));
  EXPECT_NO_THROW((void)sut_.ScopedAlignment(static_cast<uint8_t>(257)));
}

} // namespace
