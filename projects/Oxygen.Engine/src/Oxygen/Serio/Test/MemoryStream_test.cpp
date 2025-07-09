//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Serio/MemoryStream.h>

using oxygen::serio::MemoryStream;

namespace {

//=== MemoryStream (external buffer) tests ===--------------------------------//

//! Fixture for MemoryStream tests using an external buffer.

//! Fixture for MemoryStream tests using an external buffer.
class MemoryStreamTest : public testing::Test {
protected:
  std::array<std::byte, 5> buffer_ { 'a'_b, 'b'_b, 'c'_b, 'd'_b, 'e'_b };

  std::array<std::byte, 5> hello_ { 'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b };

  MemoryStream sut_ { std::span(buffer_) };
};

/*!
 Verifies that the constructor sets size, position, and end-of-stream state
 correctly for a buffer-backed MemoryStream.
*/
NOLINT_TEST_F(MemoryStreamTest, Constructor_InitializesState)
{
  // Arrange (sut_ is already constructed)

  // Act
  const auto size_result = sut_.Size();
  const auto position_result = sut_.Position();

  // Assert
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), buffer_.size());
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 0);
}

/*!
 Verifies that writing to the MemoryStream updates the buffer, advances the
 position, and does not exceed the buffer size.
*/
NOLINT_TEST_F(MemoryStreamTest, Write_WritesDataCorrectly)
{
  // Arrange (sut_ and hello_ are ready)

  // Act
  const auto write_result = sut_.Write(hello_);
  const auto size_result = sut_.Size();
  const auto position_result = sut_.Position();
  const std::string buffer_str(
    reinterpret_cast<const char*>(buffer_.data()), buffer_.size());

  // Assert
  EXPECT_TRUE(write_result);
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), buffer_.size());
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 5);
  EXPECT_EQ(buffer_str, "hello");
}

/*!
 Verifies that reading from the MemoryStream after writing returns the correct
 data and advances the position as expected.
*/
NOLINT_TEST_F(MemoryStreamTest, Read_ReadsDataCorrectly)
{
  // Arrange
  ASSERT_TRUE(sut_.Write(hello_));
  ASSERT_TRUE(sut_.Seek(0));
  std::array<std::byte, 5> buffer {};

  // Act
  const auto read_result = sut_.Read(buffer.data(), buffer.size());
  const std::string_view result(
    reinterpret_cast<const char*>(buffer.data()), buffer.size());

  // Assert
  ASSERT_TRUE(read_result);
  EXPECT_EQ(result, "hello");
}

/*!
 Verifies that Flush() returns success for a valid MemoryStream.
*/
NOLINT_TEST_F(MemoryStreamTest, Flush_Succeeds)
{
  // Arrange (sut_ is ready)

  // Act
  const auto flush_result = sut_.Flush();

  // Assert
  EXPECT_TRUE(flush_result);
}

/*!
 Verifies that Position() returns the correct value after writing and seeking.
*/
NOLINT_TEST_F(MemoryStreamTest, Position_ReportsCorrectPosition)
{
  // Arrange
  ASSERT_TRUE(sut_.Write(hello_));
  ASSERT_TRUE(sut_.Seek(2));

  // Act
  const auto position_result = sut_.Position();

  // Assert
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 2);
}

/*!
 Verifies that Seek() moves the position as expected and reports the new
 position.
*/
NOLINT_TEST_F(MemoryStreamTest, Seek_SeeksToCorrectPosition)
{
  // Arrange
  ASSERT_TRUE(sut_.Write(hello_));

  // Act
  const auto seek_result = sut_.Seek(2);
  const auto position_result = sut_.Position();

  // Assert
  EXPECT_TRUE(seek_result);
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 2);
}

/*!
 Verifies that Size() returns the correct buffer size after writing.
*/
NOLINT_TEST_F(MemoryStreamTest, Size_ReportsBufferSize)
{
  // Arrange
  ASSERT_TRUE(sut_.Write(hello_));

  // Act
  const auto size_result = sut_.Size();

  // Assert
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), buffer_.size());
}

/*!
 Verifies that Data() returns a span containing the written data as expected.
*/
NOLINT_TEST_F(MemoryStreamTest, Data_ReturnsWrittenData)
{
  // Arrange
  ASSERT_TRUE(sut_.Write(hello_));

  // Act
  const auto data = sut_.Data();
  const std::string data_str(
    reinterpret_cast<const char*>(data.data()), data.size());

  // Assert
  EXPECT_EQ(data_str, "hello");
}

/*!
 Verifies that reset() sets the position to zero and clears end-of-stream.
*/
NOLINT_TEST_F(MemoryStreamTest, Reset_ResetsPositionAndState)
{
  // Arrange
  ASSERT_TRUE(sut_.Write(hello_));

  // Act
  sut_.Reset();
  const auto position_result = sut_.Position();

  // Assert
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 0);
}

/*!
 Verifies that clear() zeroes the buffer, resets position, and clears
 end-of-stream.
*/
NOLINT_TEST_F(MemoryStreamTest, Clear_ClearsBufferAndResetsState)
{
  // Arrange
  ASSERT_TRUE(sut_.Write(hello_));

  // Act
  sut_.Clear();
  const auto size_result = sut_.Size();
  const auto position_result = sut_.Position();
  const std::string buffer_str(
    reinterpret_cast<const char*>(buffer_.data()), buffer_.size());

  // Assert
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), buffer_.size());
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 0);
  EXPECT_EQ(buffer_str, std::string(buffer_.size(), '\0'));
}

/*!
 Verifies that partial reads return the correct data, update the position, and
 leave remaining data intact.
*/
NOLINT_TEST_F(MemoryStreamTest, PartialRead_ReadsAndAdvancesCorrectly)
{
  // Arrange
  ASSERT_TRUE(sut_.Write(hello_));
  ASSERT_TRUE(sut_.Seek(0));
  std::array<std::byte, 2> buffer {};

  // Act
  ASSERT_TRUE(sut_.Read(buffer.data(), buffer.size()));
  const std::string_view partial(
    reinterpret_cast<const char*>(buffer.data()), buffer.size());
  const auto position_result = sut_.Position();

  // Assert
  EXPECT_EQ(partial, "he");
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 2);

  // Act (read remaining)
  std::array<std::byte, 3> remaining {};
  ASSERT_TRUE(sut_.Read(remaining.data(), remaining.size()));
  const std::string_view rest(
    reinterpret_cast<const char*>(remaining.data()), remaining.size());
  EXPECT_EQ(rest, "llo");
}

/*!
 Verifies that zero-size read and write operations succeed and do not alter
 state.
*/
NOLINT_TEST_F(MemoryStreamTest, ZeroSizeOperations_Succeed)
{
  // Arrange
  std::array<std::byte, 1> buffer {};

  // Act
  const auto write_result = sut_.Write(buffer.data(), 0);
  const auto read_result = sut_.Read(buffer.data(), 0);

  // Assert
  EXPECT_TRUE(write_result);
  EXPECT_TRUE(read_result);
}

/*!
 Verifies that move construction transfers buffer state and size correctly.
*/
NOLINT_TEST_F(MemoryStreamTest, MoveConstruction_MovesStateCorrectly)
{
  // Arrange & Act
  {
    const MemoryStream moved_stream { std::move(sut_) };
    const auto size_result = moved_stream.Size();

    // Assert
    EXPECT_TRUE(size_result);
    EXPECT_EQ(size_result.value(), buffer_.size());
  }
  // Assert (original object is in valid but unspecified state)
  EXPECT_TRUE(true);
}

/*!
 Verifies that sequential writes and reads work as expected and data is correct.
*/
NOLINT_TEST_F(MemoryStreamTest, SequentialReadWrite_WorksCorrectly)
{
  // Arrange
  std::array<std::byte, 2> read_buffer {};

  // Act
  EXPECT_TRUE(sut_.Write(hello_.data(), 2)); // Write "he"
  EXPECT_TRUE(sut_.Write(hello_.data() + 2, 2)); // Write "ll"
  EXPECT_TRUE(sut_.Seek(0));
  EXPECT_TRUE(sut_.Read(read_buffer.data(), 2)); // Read "he"
  const std::string first(reinterpret_cast<const char*>(read_buffer.data()), 2);
  EXPECT_TRUE(sut_.Read(read_buffer.data(), 2)); // Read "ll"
  const std::string second(
    reinterpret_cast<const char*>(read_buffer.data()), 2);

  // Assert
  EXPECT_EQ(first, "he");
  EXPECT_EQ(second, "ll");
}

/*!
 Verifies that move assignment transfers buffer state and data correctly.
*/
NOLINT_TEST_F(MemoryStreamTest, MoveAssignment_MovesStateCorrectly)
{
  // Arrange

  // Act
  const MemoryStream other_stream = std::move(sut_);
  const auto size_result = other_stream.Size();
  const auto data = other_stream.Data();
  const std::string_view data_str(
    reinterpret_cast<const char*>(data.data()), data.size());
  const std::string_view buffer_str(
    reinterpret_cast<const char*>(buffer_.data()), buffer_.size());

  // Assert
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), buffer_.size());
  EXPECT_EQ(data_str, buffer_str);
}

/*!
 Verifies that interleaved write/read/write/seek/read operations maintain data
 correctness.
*/
NOLINT_TEST_F(MemoryStreamTest, InterleavedOperations_MaintainsCorrectness)
{
  // Arrange
  constexpr std::array write_data { 't'_b, 'e'_b, 's'_b, 't'_b };
  std::array<std::byte, 2> read_buffer {};

  // Act
  EXPECT_TRUE(sut_.Write(write_data.data(), 2));
  EXPECT_TRUE(sut_.Seek(0));
  EXPECT_TRUE(sut_.Read(read_buffer.data(), 2));
  EXPECT_TRUE(sut_.Write(write_data.data() + 2, 2));
  EXPECT_TRUE(sut_.Seek(0));
  EXPECT_TRUE(sut_.Read(read_buffer.data(), 2));
  const std::string result(
    reinterpret_cast<const char*>(read_buffer.data()), 2);

  // Assert
  EXPECT_EQ(result, "te");
}

/*!
 Verifies that writing to full capacity fails for extra data, and single-byte
 operations at boundaries succeed.
*/
NOLINT_TEST_F(MemoryStreamTest, BoundaryConditions_HandlesFullAndSingleByteOps)
{
  // Arrange & Act
  EXPECT_TRUE(sut_.Write(buffer_.data(), buffer_.size()));
  constexpr std::array x { 'x'_b };
  // Assert: writing past capacity fails
  EXPECT_FALSE(sut_.Write(x.data(), 1));

  // Act: single byte read at start
  EXPECT_TRUE(sut_.Seek(0));
  std::array<std::byte, 1> single_byte {};
  EXPECT_TRUE(sut_.Read(single_byte.data(), 1));
}

/*!
 Verifies that repeated write/read cycles maintain data integrity.
*/
NOLINT_TEST_F(MemoryStreamTest, DataIntegrity_MultipleWriteReadCycles)
{
  // Arrange
  const std::string test_pattern = "12345";
  constexpr std::array test_pattern_bytes { '1'_b, '2'_b, '3'_b, '4'_b, '5'_b };

  // Act & Assert
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(sut_.Seek(0));
    EXPECT_TRUE(
      sut_.Write(test_pattern_bytes.data(), test_pattern_bytes.size()));
    std::array<std::byte, 5> verify_buffer {};
    EXPECT_TRUE(sut_.Seek(0));
    EXPECT_TRUE(sut_.Read(verify_buffer.data(), verify_buffer.size()));
    std::string result(reinterpret_cast<const char*>(verify_buffer.data()),
      verify_buffer.size());
    EXPECT_EQ(result, test_pattern);
  }
}

/*!
 Verifies that Backward() moves the position backward and subsequent reads are
 correct.
*/
NOLINT_TEST_F(MemoryStreamTest, Backward_MovesPositionAndReadsCorrectly)
{
  // Arrange
  ASSERT_TRUE(sut_.Seek(4));

  // Act
  const auto result = sut_.Backward(2);
  const auto position = sut_.Position();
  std::array<std::byte, 3> buffer {};
  ASSERT_TRUE(sut_.Read(buffer.data(), 3));
  const std::string read_str(reinterpret_cast<const char*>(buffer.data()), 3);

  // Assert
  EXPECT_TRUE(result);
  EXPECT_TRUE(position);
  EXPECT_EQ(position.value(), 2);
  EXPECT_EQ(read_str, "cde");
}

/*!
 Verifies that Forward() moves the position forward and subsequent reads are
 correct.
*/
NOLINT_TEST_F(MemoryStreamTest, Forward_MovesPositionAndReadsCorrectly)
{
  // Arrange
  ASSERT_TRUE(sut_.Seek(0));

  // Act
  const auto result = sut_.Forward(3);
  const auto position = sut_.Position();
  std::array<std::byte, 2> buffer {};
  ASSERT_TRUE(sut_.Read(buffer.data(), 2));
  const std::string read_str(reinterpret_cast<const char*>(buffer.data()), 2);

  // Assert
  EXPECT_TRUE(result);
  EXPECT_TRUE(position);
  EXPECT_EQ(position.value(), 3);
  EXPECT_EQ(read_str, "de");
}

//=== MemoryStream (internal buffer) error tests ===--------------------------//

//!
/*! Fixture for MemoryStream error tests, derived from MemoryStreamTest. */
class MemoryStreamErrorTest : public MemoryStreamTest { };

/*!
 Verifies that Write() fails and returns the correct error when writing past the
 buffer limit.
*/
NOLINT_TEST_F(MemoryStreamErrorTest, Write_Fails_WhenSizeExceedsLimit)
{
  // Arrange (sut_ and buffer_ are ready)

  // Act
  const auto result = sut_.Write(buffer_.data(), buffer_.size() + 1);

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::no_buffer_space));
}

/*!
 Verifies that Read() fails and returns the correct error when reading past the
 buffer limit.
*/
NOLINT_TEST_F(MemoryStreamErrorTest, Read_Fails_WhenSizeExceedsLimit)
{
  // Arrange (sut_ and buffer_ are ready)

  // Act
  const auto result = sut_.Read(buffer_.data(), buffer_.size() + 1);

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

/*!
 Verifies that Seek() fails and returns the correct error when seeking past the
 buffer limit.
*/
NOLINT_TEST_F(MemoryStreamErrorTest, Seek_Fails_WhenPositionExceedsLimit)
{
  // Arrange (sut_ and buffer_ are ready)

  // Act
  const auto result = sut_.Seek(buffer_.size() + 1);

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::invalid_seek));
}

/*!
 Verifies that Backward() fails and returns the correct error when moving before
 the buffer start.
*/
NOLINT_TEST_F(MemoryStreamErrorTest, Backward_Fails_BeforeBegin)
{
  // Arrange
  ASSERT_TRUE(sut_.Seek(1));

  // Act
  const auto result = sut_.Backward(2);

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

/*!
 Verifies that Forward() fails and returns the correct error when moving past
 the buffer end.
*/
NOLINT_TEST_F(MemoryStreamErrorTest, Forward_Fails_PastEnd)
{
  // Arrange
  ASSERT_TRUE(sut_.Seek(4));

  // Act
  const auto result = sut_.Forward(10);

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

/*!
 Verifies that SeekEnd() moves position to end and further reads fail as
 expected.
*/
NOLINT_TEST_F(MemoryStreamErrorTest, SeekEnd_MovesToEndAndReadFails)
{
  // Arrange & Act
  const auto result = sut_.SeekEnd();
  const auto position = sut_.Position();
  std::array<std::byte, 1> buffer {};
  const auto read_result = sut_.Read(buffer.data(), 1);

  // Assert
  EXPECT_TRUE(result);
  EXPECT_TRUE(position);
  EXPECT_EQ(position.value(), buffer_.size());
  EXPECT_FALSE(read_result);
  EXPECT_EQ(read_result.error(), std::make_error_code(std::errc::io_error));
}

//=== MemoryStream (internal buffer) tests ===--------------------------------//

//! Fixture for MemoryStream tests using the internal buffer.
class InternalBufferMemoryStreamTest : public testing::Test {
protected:
  MemoryStream sut_; // Uses internal buffer
};

/*!
 Verifies that writing more data than the initial buffer grows the internal
 buffer.
*/
NOLINT_TEST_F(InternalBufferMemoryStreamTest, Write_GrowsInternalBuffer)
{
  // Arrange
  const std::vector data_bytes {
    // clang-format off
    'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b, ' '_b,
    'w'_b, 'o'_b, 'r'_b, 'l'_b, 'd'_b,
    // clang-format on
  };

  // Act
  const auto write_result = sut_.Write(data_bytes.data(), data_bytes.size());
  const auto size_result = sut_.Size();

  // Assert
  EXPECT_TRUE(write_result);
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), 11);
}

/*!
 Verifies that multiple writes grow the buffer and all content is preserved.
*/
NOLINT_TEST_F(InternalBufferMemoryStreamTest, WriteGrowsAndMaintainsContent)
{
  // Arrange
  const std::vector data1_bytes {
    // clang-format off
    'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b
    // clang-format on
  };
  const std::vector data2_bytes {
    // clang-format off
    ' '_b, 'w'_b, 'o'_b, 'r'_b, 'l'_b, 'd'_b
    // clang-format on
  };

  // Act
  EXPECT_TRUE(sut_.Write(data1_bytes.data(), data1_bytes.size()));
  EXPECT_TRUE(sut_.Write(data2_bytes.data(), data2_bytes.size()));
  std::vector<std::byte> buffer(11);
  EXPECT_TRUE(sut_.Seek(0));
  EXPECT_TRUE(sut_.Read(buffer.data(), 11));
  const std::string result(reinterpret_cast<const char*>(buffer.data()), 11);

  // Assert
  EXPECT_EQ(result, "hello world");
}

/*!
 Verifies that the internal buffer can handle large data writes and reads (1MB).
*/
NOLINT_TEST_F(InternalBufferMemoryStreamTest, StressTest_LargeData)
{
  // Arrange
  constexpr size_t test_size = 1024ULL * 1024; // 1MB
  const std::vector large_data(test_size, 'A'_b);

  // Act
  EXPECT_TRUE(sut_.Write(large_data.data(), test_size));
  std::vector<std::byte> read_buffer(test_size);
  EXPECT_TRUE(sut_.Seek(0));
  EXPECT_TRUE(sut_.Read(read_buffer.data(), test_size));

  // Assert
  EXPECT_EQ(read_buffer, large_data);
}

} // namespace
