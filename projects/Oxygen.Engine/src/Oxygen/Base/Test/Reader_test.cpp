//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Reader.h>

#include <Oxygen/Testing/GTest.h>

#include "Mocks/MockStream.h"

using oxygen::ByteSwap;
using oxygen::IsLittleEndian;
using oxygen::serio::Reader;
using oxygen::serio::limits::SequenceSizeType;
using oxygen::serio::testing::MockStream;

namespace {

class ReaderTest : public testing::Test {
protected:
  auto SetUp() -> void override { stream_.seek(0); }

  auto write_padding(const size_t alignment) -> void
  {
    const auto pos = stream_.position().value();
    const size_t padding = (alignment - (pos % alignment)) % alignment;
    if (padding > 0) {
      const std::vector<std::byte> zeros(padding, 0x00_b);
      ASSERT_TRUE(stream_.write(zeros.data(), padding));
    }
  }

  template <typename T> auto write_pod(const T& value) -> void
  {
    // Add alignment padding for types > 1 byte
    if constexpr (sizeof(T) > 1) {
      write_padding(alignof(T));
    }

    if (!IsLittleEndian() && sizeof(T) > 1) {
      auto temp = ByteSwap(value);
      ASSERT_TRUE(
        stream_.write(reinterpret_cast<const std::byte*>(&temp), sizeof(T)));
    } else {
      ASSERT_TRUE(
        stream_.write(reinterpret_cast<const std::byte*>(&value), sizeof(T)));
    }
  }

  auto write_string(const std::string_view str) -> void
  {
    // Align length field
    write_padding(alignof(uint32_t));

    auto length = static_cast<uint32_t>(str.length());
    if (!IsLittleEndian()) {
      length = ByteSwap(length);
    }
    ASSERT_TRUE(stream_.write(
      reinterpret_cast<const std::byte*>(&length), sizeof(length)));
    ASSERT_TRUE(stream_.write(
      reinterpret_cast<const std::byte*>(str.data()), str.length()));

    // Add final alignment padding
    write_padding(alignof(uint32_t));
  }

  template <typename T> auto write_array(const std::vector<T>& values) -> void
  {
    // Align length field
    write_padding(alignof(uint32_t));
    write_pod(static_cast<uint32_t>(values.size()));

    // Align array elements if needed
    if constexpr (sizeof(T) > 1) {
      write_padding(alignof(T));
    }

    ASSERT_TRUE(stream_.write(reinterpret_cast<const std::byte*>(values.data()),
      values.size() * sizeof(T)));

    // Add final alignment padding
    write_padding(alignof(uint32_t));
  }

  auto seek_to(size_t pos) -> void { ASSERT_TRUE(stream_.seek(pos)); }

  MockStream stream_;
  Reader<MockStream> sut_ { stream_ };
};

NOLINT_TEST_F(ReaderTest, ReadPOD_Success)
{
  // Setup
  constexpr uint32_t test_int = 0x12345678;
  constexpr float test_float = 3.14f;
  write_pod(test_int);
  write_pod(test_float);

  // Prepare Read
  seek_to(0); // Reset cursor before reading
  EXPECT_EQ(stream_.position().value(), 0);

  // Read and Verify
  const auto int_result = sut_.read<uint32_t>();
  ASSERT_TRUE(int_result);
  EXPECT_EQ(int_result.value(), test_int);

  const auto float_result = sut_.read<float>();
  ASSERT_TRUE(float_result);
  EXPECT_FLOAT_EQ(float_result.value(), test_float);
}

NOLINT_TEST_F(ReaderTest, ReadString_Success)
{
  const std::string test_str = "Hello, World!";
  write_string(test_str);

  seek_to(0); // Reset cursor before reading
  EXPECT_EQ(stream_.position().value(), 0);

  const auto result = sut_.read_string();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), test_str);
}

NOLINT_TEST_F(ReaderTest, ReadEmptyString_Success)
{
  write_string("");

  seek_to(0); // Reset cursor before reading
  EXPECT_EQ(stream_.position().value(), 0);

  const auto result = sut_.read_string();
  ASSERT_TRUE(result);
  EXPECT_TRUE(result.value().empty());
}

NOLINT_TEST_F(ReaderTest, ReadString_Fails_WhenTooLarge)
{
  write_pod<SequenceSizeType>(oxygen::serio::limits::kMaxStringLength + 1);

  seek_to(0); // Reset cursor before reading
  EXPECT_EQ(stream_.position().value(), 0);

  const auto result = sut_.read_string();
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::value_too_large));
}

NOLINT_TEST_F(ReaderTest, ReadArray_Success)
{
  const std::vector<uint32_t> test_array = { 1, 2, 3, 4, 5 };
  write_array(test_array);

  seek_to(0); // Reset cursor before reading
  EXPECT_EQ(stream_.position().value(), 0);

  const auto result = sut_.read_array<uint32_t>();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), test_array);
}

NOLINT_TEST_F(ReaderTest, ReadEmptyArray_Success)
{
  const std::vector<uint32_t> empty_array;
  write_array(empty_array);

  seek_to(0); // Reset cursor before reading
  EXPECT_EQ(stream_.position().value(), 0);

  const auto result = sut_.read_array<uint32_t>();
  ASSERT_TRUE(result);
  EXPECT_TRUE(result.value().empty());
}

NOLINT_TEST_F(ReaderTest, ReadArray_Fails_WhenTooLarge)
{
  write_pod<SequenceSizeType>(oxygen::serio::limits::kMaxArrayLength + 1);

  seek_to(0); // Reset cursor before reading
  EXPECT_EQ(stream_.position().value(), 0);

  const auto result = sut_.read_array<uint32_t>();
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::value_too_large));
}

NOLINT_TEST_F(ReaderTest, Read_Fails_OnStreamError)
{
  write_pod(uint32_t { 42 });

  seek_to(0); // Reset cursor before reading
  EXPECT_EQ(stream_.position().value(), 0);

  stream_.force_fail(true);
  const auto result = sut_.read<uint32_t>();
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

NOLINT_TEST_F(ReaderTest, ReadMixedTypes_MaintainsAlignment)
{
  // Write mixed-size types
  constexpr uint8_t byte = 0x42;
  constexpr uint32_t integer = 0x12345678;
  const std::string str = "test";

  write_pod(byte);
  write_pod(integer);
  write_string(str);

  // Read and verify
  seek_to(0);

  const auto byte_result = sut_.read<uint8_t>();
  ASSERT_TRUE(byte_result);
  EXPECT_EQ(byte_result.value(), byte);

  const auto int_result = sut_.read<uint32_t>();
  ASSERT_TRUE(int_result);
  EXPECT_EQ(int_result.value(), integer);

  const auto str_result = sut_.read_string();
  ASSERT_TRUE(str_result);
  EXPECT_EQ(str_result.value(), str);
}

NOLINT_TEST_F(ReaderTest, ReadString_Fails_OnStreamError)
{
  write_string("test");

  seek_to(0); // Reset cursor before reading
  EXPECT_EQ(stream_.position().value(), 0);

  stream_.force_fail(true);
  const auto result = sut_.read_string();
  EXPECT_FALSE(result);
}

NOLINT_TEST_F(ReaderTest, ReadArray_Fails_OnStreamError)
{
  const std::vector<uint32_t> test_array = { 1, 2, 3 };
  write_array(test_array);

  seek_to(0); // Reset cursor before reading
  EXPECT_EQ(stream_.position().value(), 0);

  stream_.force_fail(true);
  const auto result = sut_.read_array<uint32_t>();
  EXPECT_FALSE(result);
}

NOLINT_TEST_F(ReaderTest, ReadBlob_Success)
{
  // Setup
  const std::vector<std::byte> test_data
    = { 'a'_b, 'b'_b, 'c'_b, 'd'_b, 'e'_b };
  ASSERT_TRUE(stream_.write(test_data.data(), test_data.size()));
  seek_to(0);

  // Act
  const auto result = sut_.read_blob(test_data.size());

  // Assert
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), test_data);
}

NOLINT_TEST_F(ReaderTest, ReadBlob_Empty)
{
  // Act
  const auto result = sut_.read_blob(0);
  // Assert
  ASSERT_TRUE(result);
  EXPECT_TRUE(result.value().empty());
}

NOLINT_TEST_F(ReaderTest, ReadBlob_Fails_OnStreamError)
{
  const std::vector<std::byte> test_data = { 'x'_b, 'y'_b };
  ASSERT_TRUE(stream_.write(test_data.data(), test_data.size()));
  seek_to(0);
  stream_.force_fail(true);
  const auto result = sut_.read_blob(test_data.size());
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

NOLINT_TEST_F(ReaderTest, ReadBlobTo_Success)
{
  // Setup
  const std::vector<std::byte> test_data = { '1'_b, '2'_b, '3'_b, '4'_b };
  ASSERT_TRUE(stream_.write(test_data.data(), test_data.size()));
  seek_to(0);
  std::vector<std::byte> buffer(test_data.size());

  // Act
  const auto result = sut_.read_blob_to(buffer);

  // Assert
  ASSERT_TRUE(result);
  EXPECT_EQ(buffer, test_data);
}

NOLINT_TEST_F(ReaderTest, ReadBlobTo_Empty)
{
  std::vector<std::byte> buffer;
  const auto result = sut_.read_blob_to(buffer);
  ASSERT_TRUE(result);
  EXPECT_TRUE(buffer.empty());
}

NOLINT_TEST_F(ReaderTest, ReadBlobTo_Fails_OnStreamError)
{
  const std::vector<std::byte> test_data = { 'z'_b, 'w'_b };
  ASSERT_TRUE(stream_.write(test_data.data(), test_data.size()));
  seek_to(0);
  std::vector<std::byte> buffer(test_data.size());
  stream_.force_fail(true);
  const auto result = sut_.read_blob_to(buffer);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

//=== Scoped Alignment Guard Integration Tests ===----------------------------//

//! Tests that Reader reads values correctly with explicit scoped alignment
//! guard.
/*! Covers auto alignment, specific alignment, and edge cases.
  @see oxygen::serio::AlignmentGuard, oxygen::serio::Reader
*/
class ReaderAlignmentGuardIntegrationTest : public testing::Test {
protected:
  auto SetUp() -> void override { stream_.seek(0); }

  auto write_aligned_uint32(uint32_t value, size_t alignment) -> void
  {
    // Write padding to align to 'alignment' boundary
    const auto pos = stream_.position().value();
    const size_t padding = (alignment - (pos % alignment)) % alignment;
    if (padding > 0) {
      const std::vector<std::byte> zeros(padding, 0x00_b);
      ASSERT_TRUE(stream_.write(zeros.data(), padding));
    }
    if (!IsLittleEndian()) {
      value = ByteSwap(value);
    }
    ASSERT_TRUE(
      stream_.write(reinterpret_cast<const std::byte*>(&value), sizeof(value)));
  }

  MockStream stream_;
  Reader<MockStream> sut_ { stream_ };
};

NOLINT_TEST_F(
  ReaderAlignmentGuardIntegrationTest, ReadsValuesWithNestedAlignmentScopes)
{
  // Arrange
  constexpr uint32_t value1 = 0x11111111;
  constexpr uint64_t value2 = 0x2222222233333333ULL;
  constexpr uint32_t value3 = 0x44444444;

  // Write value1 with 4-byte alignment
  write_aligned_uint32(value1, 4);
  // Write value2 with 8-byte alignment
  const auto pos2 = stream_.position().value();
  const size_t pad2
    = (alignof(uint64_t) - (pos2 % alignof(uint64_t))) % alignof(uint64_t);
  if (pad2 > 0) {
    const std::vector<std::byte> zeros(pad2, 0x00_b);
    ASSERT_TRUE(stream_.write(zeros.data(), pad2));
  }
  uint64_t v2 = value2;
  if (!IsLittleEndian()) {
    v2 = ByteSwap(v2);
  }
  ASSERT_TRUE(
    stream_.write(reinterpret_cast<const std::byte*>(&v2), sizeof(v2)));
  // Write value3 with 4-byte alignment
  write_aligned_uint32(value3, 4);

  // Act
  stream_.seek(0);
  // Outer scope: 4-byte alignment
  {
    auto guard4 = sut_.ScopedAlignment(4);
    auto r1 = sut_.read<uint32_t>();
    ASSERT_TRUE(r1);
    EXPECT_EQ(r1.value(), value1);

    // Nested scope: 8-byte alignment
    {
      auto guard8 = sut_.ScopedAlignment(8);
      auto r2 = sut_.read<uint64_t>();
      ASSERT_TRUE(r2);
      EXPECT_EQ(r2.value(), value2);
    }

    // Back to outer scope: 4-byte alignment
    auto r3 = sut_.read<uint32_t>();
    ASSERT_TRUE(r3);
    EXPECT_EQ(r3.value(), value3);
  }
}

NOLINT_TEST_F(
  ReaderAlignmentGuardIntegrationTest, ReadsValueWithExplicitAlignment)
{
  // Arrange
  constexpr uint32_t test_value = 0xCAFEBABE;
  constexpr size_t alignment = 16;
  write_aligned_uint32(test_value, alignment);
  // Write a second value with different alignment
  write_aligned_uint32(0xDEADBEEF, 4);

  // Act
  stream_.seek(0);
  {
    auto guard = sut_.ScopedAlignment(alignment);
    auto result = sut_.read<uint32_t>();
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), test_value);
  }
  // Next value should be readable with 4-byte alignment
  {
    auto guard = sut_.ScopedAlignment(4);
    auto result = sut_.read<uint32_t>();
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), 0xDEADBEEF);
  }
}

NOLINT_TEST_F(ReaderAlignmentGuardIntegrationTest, ReadsValueWithAutoAlignment)
{
  // Arrange
  constexpr uint32_t test_value = 0xAABBCCDD;
  // Write with 4-byte alignment (default for uint32_t)
  write_aligned_uint32(test_value, alignof(uint32_t));

  // Act
  stream_.seek(0);
  // No explicit guard: Reader should align automatically
  auto result = sut_.read<uint32_t>();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), test_value);
}

NOLINT_TEST_F(ReaderAlignmentGuardIntegrationTest,
  ReadsValueWithMisalignedData_ReadsWrongValue)
{
  // Arrange
  constexpr uint32_t test_value = 0x12345678;
  // Write value with wrong alignment (e.g., offset by 1)
  const std::vector<std::byte> pad(1, 0x00_b);
  ASSERT_TRUE(stream_.write(pad.data(), pad.size()));
  write_aligned_uint32(test_value, 1); // Actually unaligned

  // Act
  stream_.seek(0);
  // Try to read with explicit 4-byte alignment
  auto guard = sut_.ScopedAlignment(4);
  auto result = sut_.read<uint32_t>();
  // The value read will not match test_value due to misalignment
  ASSERT_TRUE(result);
  EXPECT_NE(result.value(), test_value);
}

NOLINT_TEST_F(ReaderAlignmentGuardIntegrationTest, ThrowsOnInvalidAlignment)
{
  // Arrange/Act/Assert
  EXPECT_THROW((void)sut_.ScopedAlignment(3), std::invalid_argument);
  // 0 is valid (auto-alignment)
  EXPECT_NO_THROW((void)sut_.ScopedAlignment(0));
  // 256 is valid (max alignment)
  EXPECT_NO_THROW((void)sut_.ScopedAlignment(static_cast<uint8_t>(256)));
  // 257 as uint8_t wraps to 1, which is valid
  EXPECT_NO_THROW((void)sut_.ScopedAlignment(static_cast<uint8_t>(257)));
}

NOLINT_TEST_F(
  ReaderAlignmentGuardIntegrationTest, ReadsValueWithAutoTypeAlignment)
{
  // Arrange
  constexpr uint64_t test_value = 0x1122334455667788ULL;
  // Write with 8-byte alignment (alignof(uint64_t))
  write_aligned_uint32(0xDEADBEEF, 4); // Write a dummy 4-byte value first
  // Now align to 8 and write the uint64_t
  const auto pos = stream_.position().value();
  const size_t padding
    = (alignof(uint64_t) - (pos % alignof(uint64_t))) % alignof(uint64_t);
  if (padding > 0) {
    const std::vector<std::byte> zeros(padding, 0x00_b);
    ASSERT_TRUE(stream_.write(zeros.data(), padding));
  }
  uint64_t value = test_value;
  if (!IsLittleEndian()) {
    value = ByteSwap(value);
  }
  ASSERT_TRUE(
    stream_.write(reinterpret_cast<const std::byte*>(&value), sizeof(value)));

  // Act
  stream_.seek(0);
  // Read dummy value
  {
    auto guard = sut_.ScopedAlignment(4);
    auto result = sut_.read<uint32_t>();
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), 0xDEADBEEF);
  }
  // Read uint64_t with auto-alignment (0)
  {
    auto guard = sut_.ScopedAlignment(0); // auto-align to alignof(uint64_t)
    auto result = sut_.read<uint64_t>();
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), test_value);
  }
}

} // namespace
