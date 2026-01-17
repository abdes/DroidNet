//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Serio/Reader.h>

#include "Mocks/MockStream.h"

using oxygen::ByteSwap;
using oxygen::IsLittleEndian;
using oxygen::serio::AnyReader;
using oxygen::serio::Reader;
using oxygen::serio::limits::SequenceSizeType;

using oxygen::serio::testing::MockStream;

using testing::Types;

namespace {

class ReaderBasicTest : public testing::Test {
protected:
  auto GetReader() -> Reader<MockStream>& { return sut_; }
  auto GetAnyReader() -> AnyReader& { return sut_; }

  auto WritePadding(const size_t alignment) -> void
  {
    const auto pos = stream_.Position().value();
    const size_t padding = (alignment - (pos % alignment)) % alignment;
    if (padding > 0) {
      const std::vector zeros(padding, 0x00_b);
      ASSERT_TRUE(stream_.Write(zeros.data(), padding));
    }
  }

  template <typename T> auto WritePod(const T& value) -> void
  {
    // Add alignment padding for types > 1 byte
    if constexpr (sizeof(T) > 1) {
      WritePadding(alignof(T));
    }

    if (!IsLittleEndian() && sizeof(T) > 1) {
      auto temp = ByteSwap(value);
      ASSERT_TRUE(
        stream_.Write(reinterpret_cast<const std::byte*>(&temp), sizeof(T)));
    } else {
      ASSERT_TRUE(
        stream_.Write(reinterpret_cast<const std::byte*>(&value), sizeof(T)));
    }
  }

  auto WriteString(const std::string_view str) -> void
  {
    // Align length field
    WritePadding(alignof(SequenceSizeType));

    auto length = static_cast<SequenceSizeType>(str.length());
    if (!IsLittleEndian()) {
      length = ByteSwap(length);
    }
    ASSERT_TRUE(stream_.Write(
      reinterpret_cast<const std::byte*>(&length), sizeof(length)));
    ASSERT_TRUE(stream_.Write(
      reinterpret_cast<const std::byte*>(str.data()), str.length()));
  }

  template <typename T> auto WriteArray(const std::vector<T>& values) -> void
  {
    // Align length field
    WritePadding(alignof(uint32_t));
    WritePod(static_cast<uint32_t>(values.size()));

    // Align array elements if needed
    if constexpr (sizeof(T) > 1) {
      WritePadding(alignof(T));
    }

    ASSERT_TRUE(stream_.Write(reinterpret_cast<const std::byte*>(values.data()),
      values.size() * sizeof(T)));

    // Add final alignment padding
    WritePadding(alignof(uint32_t));
  }

  auto SeekTo(size_t pos) -> void { ASSERT_TRUE(stream_.Seek(pos)); }

  auto GetStream() -> MockStream& { return stream_; }

private:
  MockStream stream_;
  Reader<MockStream> sut_ { stream_ };
};

//! Reads mixed types (byte, uint32_t, string) and maintains alignment.
NOLINT_TEST_F(ReaderBasicTest, ReadMixedTypes_MaintainsAlignment)
{
  // Write mixed-size types
  constexpr uint8_t byte = 0x42;
  constexpr uint32_t integer = 0x12345678;
  const std::string str = "test";

  WritePod(byte);
  WritePod(integer);
  WriteString(str);

  // Read and verify
  SeekTo(0);

  const auto byte_result = GetReader().Read<uint8_t>();
  ASSERT_TRUE(byte_result);
  EXPECT_EQ(byte_result.value(), byte);

  const auto int_result = GetReader().Read<uint32_t>();
  ASSERT_TRUE(int_result);
  EXPECT_EQ(int_result.value(), integer);

  const auto str_result = GetReader().Read<std::string>();
  ASSERT_TRUE(str_result);
  EXPECT_EQ(str_result.value(), str);
}

//! Reads a non-empty blob of bytes successfully from the stream.
NOLINT_TEST_F(ReaderBasicTest, ReadBlob_Success)
{
  // Setup
  const std::vector test_data = { 'a'_b, 'b'_b, 'c'_b, 'd'_b, 'e'_b };
  ASSERT_TRUE(GetStream().Write(test_data.data(), test_data.size()));
  SeekTo(0);

  // Act
  const auto result = GetReader().ReadBlob(test_data.size());

  // Assert
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), test_data);
}

//! Reads an empty blob of bytes successfully from the stream.
NOLINT_TEST_F(ReaderBasicTest, ReadBlob_Empty)
{
  // Act
  const auto result = GetReader().ReadBlob(0);
  // Assert
  ASSERT_TRUE(result);
  EXPECT_TRUE(result.value().empty());
}

//! Reads a non-empty blob of bytes into a buffer successfully.
NOLINT_TEST_F(ReaderBasicTest, ReadBlobTo_Success)
{
  // Setup
  const std::vector test_data = { '1'_b, '2'_b, '3'_b, '4'_b };
  ASSERT_TRUE(GetStream().Write(test_data.data(), test_data.size()));
  SeekTo(0);
  std::vector<std::byte> buffer(test_data.size());

  // Act
  const auto result = GetReader().ReadBlobInto(buffer);

  // Assert
  ASSERT_TRUE(result);
  EXPECT_EQ(buffer, test_data);
}

//! Reads an empty blob of bytes into a buffer successfully.
NOLINT_TEST_F(ReaderBasicTest, ReadBlobTo_Empty)
{
  std::vector<std::byte> buffer;
  const auto result = GetReader().ReadBlobInto(buffer);
  ASSERT_TRUE(result);
  EXPECT_TRUE(buffer.empty());
}

//=== Reader Error Tests ===--------------------------------------------------//

/*!
  \brief Scenario-based test for reading all standard integral types.

  This test verifies that AnyReader correctly deserializes all standard
  integral types, preserving value and byte order.

  Covers normal and boundary cases for each type.
*/
template <typename T> class ReaderIntegralTest : public ReaderBasicTest { };

using IntegralTypes = Types<std::int8_t, std::uint8_t, std::int16_t,
  std::uint16_t, std::int32_t, std::uint32_t, std::int64_t, std::uint64_t>;

TYPED_TEST_SUITE(ReaderIntegralTest, IntegralTypes);

NOLINT_TYPED_TEST(ReaderIntegralTest, ReadIntegralType)
{
  using ValueType = TypeParam;

  //! Arrange: Write a test value of the current integral type.
  ValueType value = static_cast<ValueType>(0x5A5A5A5A5A5A5A5Aull);
  this->WritePod(value);
  this->SeekTo(0);

  //! Act: Read the value back using AnyReader.
  auto result = this->GetAnyReader().template Read<ValueType>();

  //! Assert: The value matches what was written.
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), value);
}

class ReaderFloatTest : public ReaderBasicTest { };

NOLINT_TEST_F(ReaderFloatTest, ReadFloat)
{
  //! Arrange: Write a test value of the current integral type.
  constexpr float value = 1.234567f;
  this->WritePod(value);
  this->SeekTo(0);

  //! Act: Read the value back using AnyReader.
  const auto result = this->GetAnyReader().Read<float>();

  //! Assert: The value matches what was written.
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), value);
}

NOLINT_TEST_F(ReaderFloatTest, ReadDouble)
{
  //! Arrange: Write a test value of the current integral type.
  constexpr double value = 2.987654321;
  this->WritePod(value);
  this->SeekTo(0);

  //! Act: Read the value back using AnyReader.
  const auto result = this->GetAnyReader().Read<double>();

  //! Assert: The value matches what was written.
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), value);
}

class ReaderStringTest : public ReaderBasicTest { };

NOLINT_TEST_F(ReaderStringTest, ReadString)
{
  const std::string value { "Hello, World!" };
  WriteString(value);
  SeekTo(0); // Reset cursor before reading

  const auto result = GetAnyReader().Read<std::string>();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), value);
}

NOLINT_TEST_F(ReaderStringTest, ReadEmptyString)
{
  const std::string value;
  WriteString(value);
  SeekTo(0); // Reset cursor before reading

  const auto result = GetAnyReader().Read<std::string>();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), value);
}

class ReaderArrayTest : public ReaderBasicTest { };

//! Reads a non-empty array of uint32_t successfully from the stream.
NOLINT_TEST_F(ReaderArrayTest, ReadArray_Success)
{
  const std::vector<uint32_t> test_array = { 1, 2, 3, 4, 5 };
  WriteArray(test_array);

  SeekTo(0); // Reset cursor before reading

  const auto result = GetReader().Read<std::vector<uint32_t>>();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), test_array);
}

//! Reads an empty array of uint32_t successfully from the stream.
NOLINT_TEST_F(ReaderArrayTest, ReadEmptyArray_Success)
{
  const std::vector<uint32_t> empty_array;
  WriteArray(empty_array);

  SeekTo(0); // Reset cursor before reading

  const auto result = GetReader().Read<std::vector<uint32_t>>();
  ASSERT_TRUE(result);
  EXPECT_TRUE(result.value().empty());
}

//=== Reader Error Tests ===--------------------------------------------------//

//! Test fixture for error and failure scenarios in Reader.
class ReaderErrorTest : public ReaderBasicTest { };

//! Fails when reading a string that exceeds the maximum allowed length.
NOLINT_TEST_F(ReaderErrorTest, ReadString_Fails_WhenTooLarge)
{
  WritePod<SequenceSizeType>(oxygen::serio::limits::kMaxStringLength + 1);

  SeekTo(0); // Reset cursor before reading

  const auto result = GetReader().Read<std::string>();
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::value_too_large));
}

//! Fails when reading an array that exceeds the maximum allowed length.
NOLINT_TEST_F(ReaderErrorTest, ReadArray_Fails_WhenTooLarge)
{
  WritePod<SequenceSizeType>(oxygen::serio::limits::kMaxArrayLength + 1);

  SeekTo(0); // Reset cursor before reading

  const auto result = GetReader().Read<std::vector<uint32_t>>();
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::value_too_large));
}

//! Fails when reading a POD value if the stream is in a failure state.
NOLINT_TEST_F(ReaderErrorTest, Read_Fails_OnStreamError)
{
  WritePod(uint32_t { 42 });

  SeekTo(0); // Reset cursor before reading

  GetStream().ForceFail(true);
  const auto result = GetReader().Read<uint32_t>();
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

//! Fails when reading a string if the stream is in a failure state.
NOLINT_TEST_F(ReaderErrorTest, ReadString_Fails_OnStreamError)
{
  WriteString("test");

  SeekTo(0); // Reset cursor before reading

  GetStream().ForceFail(true);
  const auto result = GetReader().Read<std::string>();
  EXPECT_FALSE(result);
}

//! Fails when reading an array if the stream is in a failure state.
NOLINT_TEST_F(ReaderErrorTest, ReadArray_Fails_OnStreamError)
{
  const std::vector<uint32_t> test_array = { 1, 2, 3 };
  WriteArray(test_array);

  SeekTo(0); // Reset cursor before reading

  GetStream().ForceFail(true);
  const auto result = GetReader().Read<std::vector<int32_t>>();
  EXPECT_FALSE(result);
}

//! Fails when reading a blob if the stream is in a failure state.
NOLINT_TEST_F(ReaderErrorTest, ReadBlob_Fails_OnStreamError)
{
  const std::vector test_data = { 'x'_b, 'y'_b };
  ASSERT_TRUE(GetStream().Write(test_data.data(), test_data.size()));
  SeekTo(0);
  GetStream().ForceFail(true);
  const auto result = GetReader().ReadBlob(test_data.size());
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

//! Fails when reading a blob into a buffer if the stream is in a failure state.
NOLINT_TEST_F(ReaderErrorTest, ReadBlobTo_Fails_OnStreamError)
{
  const std::vector test_data = { 'z'_b, 'w'_b };
  ASSERT_TRUE(GetStream().Write(test_data.data(), test_data.size()));
  SeekTo(0);
  std::vector<std::byte> buffer(test_data.size());
  GetStream().ForceFail(true);
  const auto result = GetReader().ReadBlobInto(buffer);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}


//! AlignTo fails when using invalid alignment values.
NOLINT_TEST_F(ReaderErrorTest, AlignTo_Fails_OnInvalidAlignment)
{
  // Arrange
  // (No setup needed)

  // Act
  const auto zero_result = GetReader().AlignTo(0);
  const auto non_power_result = GetReader().AlignTo(3);
  const auto too_large_result = GetReader().AlignTo(512);

  // Assert
  EXPECT_FALSE(zero_result);
  EXPECT_EQ(zero_result.error(),
    std::make_error_code(std::errc::invalid_argument));
  EXPECT_FALSE(non_power_result);
  EXPECT_EQ(non_power_result.error(),
    std::make_error_code(std::errc::invalid_argument));
  EXPECT_FALSE(too_large_result);
  EXPECT_EQ(too_large_result.error(),
    std::make_error_code(std::errc::invalid_argument));
}

//=== Scoped Alignment Guard Integration Tests ===----------------------------//

//! Tests that Reader reads values correctly with explicit scoped alignment
//! guard.
/*! Covers auto alignment, specific alignment, and edge cases.
  @see oxygen::serio::AlignmentGuard, oxygen::serio::Reader
*/
class ReaderAlignmentGuardTest : public ReaderBasicTest {
protected:
  auto WriteAlignedUint32(uint32_t value, size_t alignment) -> void
  {
    // Write padding to align to 'alignment' boundary
    const auto pos = GetStream().Position().value();
    const size_t padding = (alignment - (pos % alignment)) % alignment;
    if (padding > 0) {
      const std::vector zeros(padding, 0x00_b);
      ASSERT_TRUE(GetStream().Write(zeros.data(), padding));
    }
    if (!IsLittleEndian()) {
      value = ByteSwap(value);
    }
    ASSERT_TRUE(GetStream().Write(
      reinterpret_cast<const std::byte*>(&value), sizeof(value)));
  }
};

//! Reads multiple values with nested alignment scopes and verifies correct
//! alignment.
NOLINT_TEST_F(
  ReaderAlignmentGuardTest, NestedAlignmentScopes_ReadsAllValuesCorrectly)
{
  // Arrange
  constexpr uint32_t value1 = 0x11111111;
  constexpr uint64_t value2 = 0x2222222233333333ULL;
  constexpr uint32_t value3 = 0x44444444;

  // Arrange: Write values with required alignments
  WriteAlignedUint32(value1, 4);
  const auto pos2 = GetStream().Position().value();
  const size_t pad2
    = (alignof(uint64_t) - (pos2 % alignof(uint64_t))) % alignof(uint64_t);
  if (pad2 > 0) {
    const std::vector zeros(pad2, 0x00_b);
    ASSERT_TRUE(GetStream().Write(zeros.data(), pad2));
  }
  uint64_t v2 = value2;
  if (!IsLittleEndian()) {
    v2 = ByteSwap(v2);
  }
  ASSERT_TRUE(
    GetStream().Write(reinterpret_cast<const std::byte*>(&v2), sizeof(v2)));
  WriteAlignedUint32(value3, 4);

  // Act
  GetStream().Reset();

  // Assert
  // Outer scope: 4-byte alignment
  {
    auto guard4 = GetReader().ScopedAlignment(4);
    const auto r1 = GetReader().Read<uint32_t>();
    ASSERT_TRUE(r1);
    EXPECT_EQ(r1.value(), value1);

    // Nested scope: 8-byte alignment
    {
      auto guard8 = GetReader().ScopedAlignment(8);
      const auto r2 = GetReader().Read<uint64_t>();
      ASSERT_TRUE(r2);
      EXPECT_EQ(r2.value(), value2);
    }

    // Back to outer scope: 4-byte alignment
    const auto r3 = GetReader().Read<uint32_t>();
    ASSERT_TRUE(r3);
    EXPECT_EQ(r3.value(), value3);
  }
}

//! Reads values with explicit alignment and verifies correct alignment.
NOLINT_TEST_F(ReaderAlignmentGuardTest, ExplicitAlignment_ReadsValuesCorrectly)
{
  // Arrange
  constexpr uint32_t test_value = 0xCAFEBABE;
  constexpr size_t alignment = 16;
  WriteAlignedUint32(test_value, alignment);
  WriteAlignedUint32(0xDEADBEEF, 4);

  // Act
  GetStream().Reset();

  // Assert
  {
    auto guard = GetReader().ScopedAlignment(alignment);
    const auto result = GetReader().Read<uint32_t>();
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), test_value);
  }
  {
    auto guard = GetReader().ScopedAlignment(4);
    const auto result = GetReader().Read<uint32_t>();
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), 0xDEADBEEF);
  }
}

//! Reads a value with automatic alignment (no explicit guard).
NOLINT_TEST_F(ReaderAlignmentGuardTest, AutoAlignment_ReadsValueCorrectly)
{
  // Arrange
  constexpr uint32_t test_value = 0xAABBCCDD;
  WriteAlignedUint32(test_value, alignof(uint32_t));

  // Act
  GetStream().Reset();
  // No explicit guard: Reader should align automatically

  // Assert
  const auto result = GetReader().Read<uint32_t>();
  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), test_value);
}

//! Reads a value with misaligned data and expects a wrong value due to
//! misalignment.
NOLINT_TEST_F(ReaderAlignmentGuardTest, MisalignedData_ReadsWrongValue)
{
  // Arrange
  constexpr uint32_t test_value = 0x12345678;
  // Arrange: Write value with wrong alignment (e.g., offset by 1)
  const std::vector pad(1, 0x00_b);
  ASSERT_TRUE(GetStream().Write(pad.data(), pad.size()));
  WriteAlignedUint32(test_value, 1); // Actually unaligned

  // Act
  GetStream().Reset();

  // Assert
  // Try to read with explicit 4-byte alignment
  auto guard = GetReader().ScopedAlignment(4);
  const auto result = GetReader().Read<uint32_t>();
  // The value read will not match test_value due to misalignment
  ASSERT_TRUE(result);
  EXPECT_NE(result.value(), test_value);
}

//! Throws on invalid alignment values for ScopedAlignment.
NOLINT_TEST_F(ReaderAlignmentGuardTest, ScopedAlignment_InvalidAlignment_Throws)
{
  // Arrange/Act/Assert
  // : Throws on invalid alignment
  EXPECT_THROW((void)GetReader().ScopedAlignment(3), std::invalid_argument);
  // Assert: 0 is valid (auto-alignment)
  EXPECT_NO_THROW((void)GetReader().ScopedAlignment(0));
  // Assert: 256 is valid (max alignment)
  EXPECT_NO_THROW((void)GetReader().ScopedAlignment(static_cast<uint16_t>(256)));
  // Assert: 257 is invalid (exceeds max)
  EXPECT_THROW((void)GetReader().ScopedAlignment(static_cast<uint16_t>(257)),
    std::invalid_argument);
}

//! Reads a value with auto type alignment (alignof(T)) and verifies correct
//! alignment.
NOLINT_TEST_F(ReaderAlignmentGuardTest, AutoTypeAlignment_ReadsValueCorrectly)
{
  // Arrange
  constexpr uint64_t test_value = 0x1122334455667788ULL;
  // Arrange: Write with 8-byte alignment (alignof(uint64_t))
  WriteAlignedUint32(0xDEADBEEF, 4); // Write a dummy 4-byte value first
  const auto pos = GetStream().Position().value();
  const size_t padding
    = (alignof(uint64_t) - (pos % alignof(uint64_t))) % alignof(uint64_t);
  if (padding > 0) {
    const std::vector zeros(padding, 0x00_b);
    ASSERT_TRUE(GetStream().Write(zeros.data(), padding));
  }
  uint64_t value = test_value;
  if (!IsLittleEndian()) {
    value = ByteSwap(value);
  }
  ASSERT_TRUE(GetStream().Write(
    reinterpret_cast<const std::byte*>(&value), sizeof(value)));

  // Act
  GetStream().Reset();

  // Assert
  // Read dummy value
  {
    auto guard = GetReader().ScopedAlignment(4);
    const auto result = GetReader().Read<uint32_t>();
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), 0xDEADBEEF);
  }
  // Read uint64_t with auto-alignment (0)
  {
    auto guard
      = GetReader().ScopedAlignment(0); // auto-align to alignof(uint64_t)
    const auto result = GetReader().Read<uint64_t>();
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), test_value);
  }
}

} // namespace
