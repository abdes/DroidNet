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
  void SetUp() override { stream_.seek(0); }

  void write_padding(const size_t alignment)
  {
    const auto pos = stream_.position().value();
    const size_t padding = (alignment - (pos % alignment)) % alignment;
    if (padding > 0) {
      const std::vector<std::byte> zeros(padding, 0x00_b);
      ASSERT_TRUE(stream_.write(zeros.data(), padding));
    }
  }

  template <typename T> void write_pod(const T& value)
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

  void write_string(const std::string_view str)
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

  template <typename T> void write_array(const std::vector<T>& values)
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

  void seek_to(size_t pos) { ASSERT_TRUE(stream_.seek(pos)); }

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
  const uint8_t byte = 0x42;
  const uint32_t integer = 0x12345678;
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

} // namespace
