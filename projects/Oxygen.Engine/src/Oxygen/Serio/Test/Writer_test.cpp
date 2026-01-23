//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "Mocks/MockStream.h"
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::ByteSwap;
using oxygen::IsLittleEndian;
using oxygen::serio::AnyWriter;
using oxygen::serio::Writer;
using oxygen::serio::limits::SequenceSizeType;
using oxygen::serio::testing::MockStream;

using testing::Types;

namespace {

//=== Writer Basic Tests ===--------------------------------------------------//

class WriterBasicTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    stream_.Reset();
    verify_pos_ = 0;
  }

  auto GetWriter() -> Writer<MockStream>& { return writer_; }
  auto GetAnyWriter() -> AnyWriter& { return writer_; }
  auto GetStream() -> MockStream& { return stream_; }

  template <typename T> auto VerifyWritten(const T& expected) -> void
  {
    // Align verify_pos_ to alignof(T)
    const size_t padding
      = (alignof(T) - (verify_pos_ % alignof(T))) % alignof(T);
    verify_pos_ += padding;
    ASSERT_EQ(verify_pos_ % alignof(T), 0)
      << "Data not properly aligned at position " << verify_pos_;

    ASSERT_GE(stream_.Data().size(), verify_pos_ + sizeof(T));

    T actual;
    std::memcpy(&actual, stream_.Data().data() + verify_pos_, sizeof(T));
    if (!IsLittleEndian() && sizeof(T) > 1) {
      actual = ByteSwap(actual);
    }
    EXPECT_EQ(actual, expected);
    verify_pos_ += sizeof(T);
  }

  auto VerifyWrittenString(const std::string& expected) -> void
  {
    // Verify length field alignment
    ASSERT_EQ(verify_pos_ % alignof(uint32_t), 0);

    uint32_t length;
    ASSERT_GE(stream_.Data().size(), verify_pos_ + sizeof(uint32_t));
    std::memcpy(&length, stream_.Data().data() + verify_pos_, sizeof(length));
    if (!IsLittleEndian()) {
      length = ByteSwap(length);
    }
    verify_pos_ += sizeof(uint32_t);

    ASSERT_EQ(length, expected.length());
    ASSERT_GE(stream_.Data().size(), verify_pos_ + length);

    const std::string actual(
      reinterpret_cast<const char*>(stream_.Data().data()) + verify_pos_,
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
  Writer<MockStream> writer_ { stream_ };
  size_t verify_pos_ { 0 };
};

//! Writes mixed types (byte, uint32_t, string) and maintains alignment.
/*!
  Verifies that Writer writes mixed-size types with correct alignment.
*/
NOLINT_TEST_F(WriterBasicTest, WriteMixedTypes_MaintainsAlignment)
{
  // Arrange
  constexpr uint8_t byte = 0x42;
  constexpr uint32_t integer = 0x12345678;
  const std::string str = "test";

  // Act
  ASSERT_TRUE(GetWriter().Write(byte));
  ASSERT_TRUE(GetWriter().Write(integer));
  ASSERT_TRUE(GetWriter().Write(str));

  // Assert
  TRACE_GCHECK_F(VerifyWritten(byte), "byte")
  TRACE_GCHECK_F(VerifyWritten(integer), "integer")
  TRACE_GCHECK_F(VerifyWrittenString(str), "str")
}

//! Writes a non-empty blob of bytes successfully to the stream.
NOLINT_TEST_F(WriterBasicTest, WriteBlob_Success)
{
  // Arrange
  const std::vector<std::byte> test_data
    = { std::byte { 'a' }, std::byte { 'b' }, std::byte { 'c' },
        std::byte { 'd' }, std::byte { 'e' } };

  // Act
  ASSERT_TRUE(GetWriter().WriteBlob(test_data));

  // Assert
  ASSERT_GE(GetStream().Data().size(), test_data.size());
  EXPECT_TRUE(
    std::equal(test_data.begin(), test_data.end(), GetStream().Data().begin()));
}

//! Writes an empty blob of bytes successfully to the stream.
NOLINT_TEST_F(WriterBasicTest, WriteBlob_Empty)
{
  // Arrange
  const std::vector<std::byte> empty_data;

  // Act
  ASSERT_TRUE(GetWriter().WriteBlob(empty_data));

  // Assert
  EXPECT_TRUE(GetStream().Data().empty());
}

//! Writes a non-empty string successfully to the stream.
NOLINT_TEST_F(WriterBasicTest, WriteString_Success)
{
  // Arrange
  const std::string test_str = "Hello, World!";

  // Act
  ASSERT_TRUE(GetWriter().Write(test_str));

  // Assert
  TRACE_GCHECK_F(VerifyWrittenString(test_str), "test_str")
}

//! Writes an empty string successfully to the stream.
NOLINT_TEST_F(WriterBasicTest, WriteEmptyString_Success)
{
  // Act
  ASSERT_TRUE(GetWriter().Write(std::string {}));

  // Assert
  TRACE_GCHECK_F(VerifyWrittenString(""), "empty-str")
}

//! Writes a non-empty array of uint32_t successfully to the stream.
NOLINT_TEST_F(WriterBasicTest, WriteArray_Success)
{
  // Arrange
  const std::vector<uint32_t> test_array = { 1, 2, 3, 4, 5 };

  // Act
  ASSERT_TRUE(GetWriter().Write(test_array));

  // Assert
  TRACE_GCHECK_F(
    VerifyWritten(static_cast<uint32_t>(test_array.size())), "size")
  for (const auto& value : test_array) {
    TRACE_GCHECK_F(VerifyWritten(value), "value")
  }
}

//! Writes an empty array of uint32_t successfully to the stream.
NOLINT_TEST_F(WriterBasicTest, WriteEmptyArray_Success)
{
  // Arrange
  const std::vector<uint32_t> empty_array;

  // Act
  ASSERT_TRUE(GetWriter().Write(empty_array));

  // Assert
  TRACE_GCHECK_F(VerifyWritten(static_cast<uint32_t>(0)), "size")
}

//=== Writer Error Tests ===--------------------------------------------------//

class WriterErrorTest : public WriterBasicTest { };

//! Fails when writing a string that exceeds the maximum allowed length.
NOLINT_TEST_F(WriterErrorTest, WriteString_Fails_WhenTooLarge)
{
  // Arrange
  const std::string large_str(oxygen::serio::limits::kMaxStringLength + 1, 'x');

  // Act
  const auto result = GetWriter().Write(large_str);

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::value_too_large));
}

//! Fails when writing an array that exceeds the maximum allowed length.
NOLINT_TEST_F(WriterErrorTest, WriteArray_Fails_WhenTooLarge)
{
  // Arrange
  const std::vector<uint32_t> large_array(
    oxygen::serio::limits::kMaxArrayLength + 1);

  // Act
  const auto result = GetWriter().Write(large_array);

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::message_size));
}

//! Fails when writing a blob if the stream is in a failure state.
NOLINT_TEST_F(WriterErrorTest, WriteBlob_Fails_OnStreamError)
{
  // Arrange
  const std::vector<std::byte> test_data
    = { std::byte { 'x' }, std::byte { 'y' } };
  GetStream().ForceFail(true);

  // Act
  const auto result = GetWriter().WriteBlob(test_data);

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

//! Fails when writing a string if the stream is in a failure state.
NOLINT_TEST_F(WriterErrorTest, WriteString_Fails_OnStreamError)
{
  // Arrange
  GetStream().ForceFail(true);

  // Act
  const auto result = GetWriter().Write(std::string("fail"));

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

//! Fails when writing an array if the stream is in a failure state.
NOLINT_TEST_F(WriterErrorTest, WriteArray_Fails_OnStreamError)
{
  // Arrange
  GetStream().ForceFail(true);
  const std::vector<uint32_t> arr = { 1, 2, 3 };

  // Act
  const auto result = GetWriter().Write(arr);

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

//! AlignTo fails when using invalid alignment values.
NOLINT_TEST_F(WriterErrorTest, AlignTo_Fails_OnInvalidAlignment)
{
  // Arrange
  // (No setup needed)

  // Act
  const auto zero_result = GetWriter().AlignTo(0);
  const auto non_power_result = GetWriter().AlignTo(3);
  const auto too_large_result = GetWriter().AlignTo(512);

  // Assert
  EXPECT_FALSE(zero_result);
  EXPECT_EQ(
    zero_result.error(), std::make_error_code(std::errc::invalid_argument));
  EXPECT_FALSE(non_power_result);
  EXPECT_EQ(non_power_result.error(),
    std::make_error_code(std::errc::invalid_argument));
  EXPECT_FALSE(too_large_result);
  EXPECT_EQ(too_large_result.error(),
    std::make_error_code(std::errc::invalid_argument));
}

//=== AnyWriter API Tests ===-------------------------------------------------//

template <typename T> class WriterIntegralTest : public WriterBasicTest { };

using IntegralTypes = Types<std::int8_t, std::uint8_t, std::int16_t,
  std::uint16_t, std::int32_t, std::uint32_t, std::int64_t, std::uint64_t>;

TYPED_TEST_SUITE(WriterIntegralTest, IntegralTypes);

//! Writes using AnyWriter interface for all standard integral types.
/*!
  Verifies that AnyWriter correctly serializes all standard integral types.
*/
NOLINT_TYPED_TEST(WriterIntegralTest, WriteIntegralType)
{
  // Arrange
  using ValueType = TypeParam;
  ValueType value = static_cast<ValueType>(0x5A5A5A5A5A5A5A5Aull);

  // Act
  ASSERT_TRUE(this->GetAnyWriter().Write(value));

  // Assert
  this->VerifyWritten(value);
}

//! Writes floating point types using AnyWriter.
class WriterFloatTest : public WriterBasicTest { };

NOLINT_TEST_F(WriterFloatTest, WriteFloat)
{
  // Arrange
  constexpr float value = 1.234567f;

  // Act
  ASSERT_TRUE(GetAnyWriter().Write(value));

  // Assert
  VerifyWritten(value);
}

NOLINT_TEST_F(WriterFloatTest, WriteDouble)
{
  // Arrange
  constexpr double value = std::numbers::pi;

  // Act
  ASSERT_TRUE(GetAnyWriter().Write(value));

  // Assert
  VerifyWritten(value);
}

//=== Scoped Alignment Guard Integration Tests ===----------------------------//

class WriterAlignmentGuardTest : public WriterBasicTest { };

//! Writes a value with explicit alignment and verifies correct alignment.
NOLINT_TEST_F(WriterAlignmentGuardTest, ExplicitAlignment_WritesValueCorrectly)
{
  // Arrange
  constexpr uint32_t test_value = 0xCAFEBABE;

  // Act
  {
    constexpr size_t alignment = 16;
    auto guard = GetWriter().ScopedAlignment(alignment);
    ASSERT_TRUE(GetWriter().Write(test_value));
  }

  // Assert
  VerifyWritten(test_value);
}

//! Writes a value with automatic alignment (no explicit guard).
NOLINT_TEST_F(WriterAlignmentGuardTest, AutoAlignment_WritesValueCorrectly)
{
  // Arrange
  constexpr uint32_t test_value = 0xAABBCCDD;

  // Act
  {
    auto guard = GetWriter().ScopedAlignment(0);
    ASSERT_TRUE(GetWriter().Write(test_value));
  }

  // Assert
  VerifyWritten(test_value);
}

//! Writes multiple values with nested alignment scopes and verifies correct
//! alignment.
NOLINT_TEST_F(
  WriterAlignmentGuardTest, NestedAlignmentScopes_WritesAllValuesCorrectly)
{
  // Arrange
  constexpr uint32_t value1 = 0x11111111;
  constexpr uint64_t value2 = 0x2222222233333333ULL;
  constexpr uint32_t value3 = 0x44444444;

  // Act
  {
    auto guard4 = GetWriter().ScopedAlignment(4);
    ASSERT_TRUE(GetWriter().Write(value1));
    {
      auto guard8 = GetWriter().ScopedAlignment(8);
      ASSERT_TRUE(GetWriter().Write(value2));
    }
    ASSERT_TRUE(GetWriter().Write(value3));
  }

  // Assert
  VerifyWritten(value1);
  VerifyWritten(value2);
  VerifyWritten(value3);
}

//! Throws on invalid alignment values for ScopedAlignment.
NOLINT_TEST_F(WriterAlignmentGuardTest, ScopedAlignment_InvalidAlignment_Throws)
{
  // Act & Assert
  EXPECT_THROW((void)GetWriter().ScopedAlignment(3), std::invalid_argument);
  EXPECT_NO_THROW((void)GetWriter().ScopedAlignment(0));
  EXPECT_NO_THROW(
    (void)GetWriter().ScopedAlignment(static_cast<uint16_t>(256)));
  EXPECT_THROW((void)GetWriter().ScopedAlignment(static_cast<uint16_t>(257)),
    std::invalid_argument);
}

//! Writes a value with auto type alignment (alignof(T)) and verifies correct
//! alignment.
NOLINT_TEST_F(WriterAlignmentGuardTest, AutoTypeAlignment_WritesValueCorrectly)
{
  // Arrange
  constexpr uint64_t value = 0xDEADBEEFCAFEBABEULL;

  // Act
  {
    auto guard = GetWriter().ScopedAlignment(alignof(uint64_t));
    ASSERT_TRUE(GetWriter().Write(value));
  }

  // Assert
  VerifyWritten(value);
}

//=== Additional/Edge/Boundary Tests ===-------------------------------------//

//! Writes a large blob up to the maximum allowed size.
NOLINT_TEST_F(WriterBasicTest, WriteBlob_MaxSize)
{
  // Arrange
  constexpr size_t max_size
    = static_cast<size_t>(1024) * 1024; // 1MB for test, adjust as needed
  std::vector large_blob(max_size, std::byte { 0xAB });

  // Act
  const auto result = GetWriter().WriteBlob(large_blob);

  // Assert
  ASSERT_TRUE(result);
  ASSERT_GE(GetStream().Data().size(), max_size);
  EXPECT_TRUE(std::equal(
    large_blob.begin(), large_blob.end(), GetStream().Data().begin()));
}

//! Flush succeeds when stream is healthy.
NOLINT_TEST_F(WriterBasicTest, Flush_Succeeds)
{
  // Act
  const auto result = GetWriter().Flush();

  // Assert
  ASSERT_TRUE(result);
}

//! Flush fails when stream is in error state.
NOLINT_TEST_F(WriterErrorTest, Flush_Fails_OnStreamError)
{
  // Arrange
  GetStream().ForceFail(true);

  // Act
  const auto result = GetWriter().Flush();

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

//! Position returns correct value after writes.
NOLINT_TEST_F(WriterBasicTest, Position_AfterWrites)
{
  // Arrange
  constexpr uint32_t value = 0x12345678;
  ASSERT_TRUE(GetWriter().Write(value));
  const auto pos_result = GetWriter().Position();

  // Assert
  ASSERT_TRUE(pos_result);
  EXPECT_EQ(pos_result.value(), sizeof(uint32_t));
}

//! Position fails when stream is in error state.
NOLINT_TEST_F(WriterErrorTest, Position_Fails_OnStreamError)
{
  // Arrange
  GetStream().ForceFail(true);

  // Act
  const auto pos_result = GetWriter().Position();

  // Assert
  EXPECT_FALSE(pos_result);
  EXPECT_EQ(pos_result.error(), std::make_error_code(std::errc::io_error));
}

} //
