//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
// Covers: Serio/Reader.h

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "gtest/gtest.h"

#include <Oxygen/Base/Endian.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Record.h>
#include <Oxygen/Serio/Region.h>
#include <Oxygen/Serio/RegionStream.h>
#include <Oxygen/Serio/Stream.h>
#include <Oxygen/Serio/Testing/FaultInjectingDevice.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::ByteSwap;
using oxygen::IsLittleEndian;
using oxygen::serio::AnyReader;
using oxygen::serio::ByteOffset;
using oxygen::serio::Error;
using oxygen::serio::MemoryFile;
using oxygen::serio::MemoryStream;
using oxygen::serio::Reader;
using oxygen::serio::Region;
using oxygen::serio::RegionReadStream;
using oxygen::serio::limits::SequenceSizeType;
using oxygen::serio::testing::FaultInjectingDevice;
using oxygen::serio::testing::FaultOperation;

using testing::Types;

namespace {
//! A record declared over a padded layout.
struct PaddedRecord {
  uint8_t tag { 0 };
  uint32_t value { 0 };
};
} // namespace

template <> inline constexpr bool oxygen::serio::kIsRecord<PaddedRecord> = true;

// A sequence of a record that fails its layout check never takes the bulk
// copy: it reaches the record's own Load, whose assertion rejects it.
static_assert(oxygen::serio::Record<PaddedRecord>);
static_assert(!oxygen::serio::RecordLayout<PaddedRecord>);
static_assert(!oxygen::serio::BulkElement<PaddedRecord>);

namespace {

class ReaderBasicTest : public testing::Test {
protected:
  auto GetReader() -> Reader<MemoryStream>& { return sut_; }
  auto GetAnyReader() -> AnyReader& { return sut_; }

  auto WritePadding(const size_t alignment) -> void
  {
    const auto pos = static_cast<size_t>(stream_.Position().get());
    const size_t padding = (alignment - (pos % alignment)) % alignment;
    if (padding > 0) {
      const std::vector zeros(padding, 0x00_b);
      ASSERT_TRUE(stream_.Write(zeros));
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
        stream_.Write(std::as_bytes(std::span(&temp, static_cast<size_t>(1)))));
    } else {
      ASSERT_TRUE(stream_.Write(
        std::as_bytes(std::span(&value, static_cast<size_t>(1)))));
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
    ASSERT_TRUE(
      stream_.Write(std::as_bytes(std::span(&length, static_cast<size_t>(1)))));
    ASSERT_TRUE(stream_.Write(std::as_bytes(std::span(str))));
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

    ASSERT_TRUE(stream_.Write(std::as_bytes(std::span(values))));

    // Add final alignment padding
    WritePadding(alignof(uint32_t));
  }

  auto SeekTo(const size_t pos) -> void
  {
    ASSERT_TRUE(stream_.Seek(ByteOffset { pos }));
  }

  auto GetStream() -> MemoryStream& { return stream_; }

private:
  MemoryStream stream_;
  Reader<MemoryStream> sut_ { stream_ };
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
  ASSERT_TRUE(GetStream().Write(test_data));
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
  ASSERT_TRUE(GetStream().Write(test_data));
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
  auto value = static_cast<ValueType>(0x5A5A5A5A5A5A5A5AULL);
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
  constexpr float value = 1.234567F;
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
/*!
  `InjectReadFault` copies the bytes written so far into a fault-injecting
  device whose next read fails with `kIo`, and exposes a reader over it. The
  device outlives the stream and the stream outlives the reader, as the members
  are destroyed in reverse order of declaration.
*/
class ReaderErrorTest : public ReaderBasicTest {
protected:
  using FaultyDevice = FaultInjectingDevice<MemoryFile>;
  using FaultyStream = RegionReadStream<FaultyDevice>;

  auto InjectReadFault() -> void
  {
    device_.emplace(MemoryFile::FromBytes(GetStream().Data()).value());
    device_->FailOn(FaultOperation::kRead, 0, make_error_code(Error::kIo));
    faulty_stream_.emplace(Region<FaultyDevice>::Whole(*device_).value());
    faulty_reader_ = std::make_unique<Reader<FaultyStream>>(*faulty_stream_);
  }

  auto GetFaultyReader() -> Reader<FaultyStream>& { return *faulty_reader_; }

private:
  std::optional<FaultyDevice> device_;
  std::optional<FaultyStream> faulty_stream_;
  std::unique_ptr<Reader<FaultyStream>> faulty_reader_;
};

//! Fails when reading a string that exceeds the maximum allowed length.
NOLINT_TEST_F(ReaderErrorTest, ReadString_Fails_WhenTooLarge)
{
  WritePod<SequenceSizeType>(oxygen::serio::limits::kMaxStringLength + 1);

  SeekTo(0); // Reset cursor before reading

  const auto result = GetReader().Read<std::string>();
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), Error::kTooLarge);
}

//! Fails when reading an array that exceeds the maximum allowed length.
NOLINT_TEST_F(ReaderErrorTest, ReadArray_Fails_WhenTooLarge)
{
  WritePod<SequenceSizeType>(oxygen::serio::limits::kMaxArrayLength + 1);

  SeekTo(0); // Reset cursor before reading

  const auto result = GetReader().Read<std::vector<uint32_t>>();
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), Error::kTooLarge);
}

//! A length whose product with a huge element size passes `uint64_t` is too
//! large: the check never wraps to a small number.
NOLINT_TEST_F(ReaderErrorTest, ReadSequenceSize_Fails_WhenProductWraps)
{
  WritePod<SequenceSizeType>(2);
  WritePod<uint64_t>(0);
  SeekTo(0);
  auto size = SequenceSizeType { 0 };

  const auto result = GetAnyReader().ReadSequenceSize(
    size, oxygen::serio::limits::kMaxArrayLength, size_t { 1 } << 63U);

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), Error::kTooLarge);
  EXPECT_EQ(size, 0U);
}

//! A vector of non-bulk elements whose length passes the elements the stream
//! holds fails at the first missing element.
NOLINT_TEST_F(ReaderErrorTest, ReadArrayOfStrings_Fails_WhenElementsRunOut)
{
  WritePod<SequenceSizeType>(3);
  WriteString("only");
  SeekTo(0);

  const auto result = GetReader().Read<std::vector<std::string>>();

  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), Error::kEndOfData);
}

//! Fails when reading a POD value if the device fails.
NOLINT_TEST_F(ReaderErrorTest, Read_Fails_OnStreamError)
{
  WritePod(uint32_t { 42 });

  SeekTo(0); // Reset cursor before reading

  InjectReadFault();
  const auto result = GetFaultyReader().Read<uint32_t>();
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), Error::kIo);
}

//! Fails when reading a string if the device fails.
NOLINT_TEST_F(ReaderErrorTest, ReadString_Fails_OnStreamError)
{
  WriteString("test");

  SeekTo(0); // Reset cursor before reading

  InjectReadFault();
  const auto result = GetFaultyReader().Read<std::string>();
  EXPECT_FALSE(result);
}

//! Fails when reading an array if the device fails.
NOLINT_TEST_F(ReaderErrorTest, ReadArray_Fails_OnStreamError)
{
  const std::vector<uint32_t> test_array = { 1, 2, 3 };
  WriteArray(test_array);

  SeekTo(0); // Reset cursor before reading

  InjectReadFault();
  const auto result = GetFaultyReader().Read<std::vector<int32_t>>();
  EXPECT_FALSE(result);
}

//! Fails when reading a blob if the device fails.
NOLINT_TEST_F(ReaderErrorTest, ReadBlob_Fails_OnStreamError)
{
  const std::vector test_data = { 'x'_b, 'y'_b };
  ASSERT_TRUE(GetStream().Write(test_data));
  SeekTo(0);
  InjectReadFault();
  const auto result = GetFaultyReader().ReadBlob(test_data.size());
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), Error::kIo);
}

//! Fails when reading a blob into a buffer if the device fails.
NOLINT_TEST_F(ReaderErrorTest, ReadBlobTo_Fails_OnStreamError)
{
  const std::vector test_data = { 'z'_b, 'w'_b };
  ASSERT_TRUE(GetStream().Write(test_data));
  SeekTo(0);
  std::vector<std::byte> buffer(test_data.size());
  InjectReadFault();
  const auto result = GetFaultyReader().ReadBlobInto(buffer);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), Error::kIo);
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
  EXPECT_EQ(
    zero_result.error(), std::make_error_code(std::errc::invalid_argument));
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
  auto WriteAlignedUint32(uint32_t value, const std::align_val_t boundary)
    -> void
  {
    // Write padding to align to the 'boundary'
    const auto alignment = static_cast<size_t>(boundary);
    const auto pos = static_cast<size_t>(GetStream().Position().get());
    const size_t padding = (alignment - (pos % alignment)) % alignment;
    if (padding > 0) {
      const std::vector zeros(padding, 0x00_b);
      ASSERT_TRUE(GetStream().Write(zeros));
    }
    if (!IsLittleEndian()) {
      value = ByteSwap(value);
    }
    ASSERT_TRUE(GetStream().Write(
      std::as_bytes(std::span(&value, static_cast<size_t>(1)))));
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
  WriteAlignedUint32(value1, std::align_val_t { 4 });
  const auto pos2 = static_cast<size_t>(GetStream().Position().get());
  const size_t pad2
    = (alignof(uint64_t) - (pos2 % alignof(uint64_t))) % alignof(uint64_t);
  if (pad2 > 0) {
    const std::vector zeros(pad2, 0x00_b);
    ASSERT_TRUE(GetStream().Write(zeros));
  }
  uint64_t v2 = value2;
  if (!IsLittleEndian()) {
    v2 = ByteSwap(v2);
  }
  ASSERT_TRUE(
    GetStream().Write(std::as_bytes(std::span(&v2, static_cast<size_t>(1)))));
  WriteAlignedUint32(value3, std::align_val_t { 4 });

  // Act
  SeekTo(0);

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
  WriteAlignedUint32(test_value, std::align_val_t { alignment });
  WriteAlignedUint32(0xDEADBEEF, std::align_val_t { 4 });

  // Act
  SeekTo(0);

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
  WriteAlignedUint32(test_value, std::align_val_t { alignof(uint32_t) });

  // Act
  SeekTo(0);
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
  ASSERT_TRUE(GetStream().Write(pad));
  // Actually unaligned
  WriteAlignedUint32(test_value, std::align_val_t { 1 });

  // Act
  SeekTo(0);

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
  EXPECT_NO_THROW(
    (void)GetReader().ScopedAlignment(static_cast<uint16_t>(256)));
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
  // Write a dummy 4-byte value first
  WriteAlignedUint32(0xDEADBEEF, std::align_val_t { 4 });
  const auto pos = static_cast<size_t>(GetStream().Position().get());
  const size_t padding
    = (alignof(uint64_t) - (pos % alignof(uint64_t))) % alignof(uint64_t);
  if (padding > 0) {
    const std::vector zeros(padding, 0x00_b);
    ASSERT_TRUE(GetStream().Write(zeros));
  }
  uint64_t value = test_value;
  if (!IsLittleEndian()) {
    value = ByteSwap(value);
  }
  ASSERT_TRUE(GetStream().Write(
    std::as_bytes(std::span(&value, static_cast<size_t>(1)))));

  // Act
  SeekTo(0);

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
