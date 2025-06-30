//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Base/MemoryStream.h>

#include <Oxygen/Testing/GTest.h>

using namespace oxygen::serio;
using namespace testing;

class MemoryStreamTest : public Test {
protected:
  std::array<std::byte, 5> buffer_ {
    'a'_b,
    'b'_b,
    'c'_b,
    'd'_b,
    'e'_b,
  };

  std::array<const std::byte, 5> hello_ {
    'h'_b,
    'e'_b,
    'l'_b,
    'l'_b,
    'o'_b,
  };

  MemoryStream sut_ { std::span(buffer_) };
};

NOLINT_TEST_F(MemoryStreamTest, Constructor_Success)
{
  const auto size_result = sut_.size();
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), buffer_.size());

  const auto position_result = sut_.position();
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 0);

  EXPECT_FALSE(sut_.eof());
}

NOLINT_TEST_F(MemoryStreamTest, Write_Success)
{
  const auto write_result = sut_.write(hello_);
  EXPECT_TRUE(write_result);

  const auto size_result = sut_.size();
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), buffer_.size());

  const auto position_result = sut_.position();
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 5);

  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(buffer_.data()), buffer_.size()),
    "hello");
}

NOLINT_TEST_F(MemoryStreamTest, Read_Success)
{
  const auto write_result = sut_.write(hello_);
  EXPECT_TRUE(write_result);

  const auto seek_result = sut_.seek(0);
  EXPECT_TRUE(seek_result);

  std::array<std::byte, 5> buffer {};
  ASSERT_TRUE(sut_.read(buffer.data(), buffer.size()));

  std::string_view result(
    reinterpret_cast<const char*>(buffer.data()), buffer.size());
  EXPECT_EQ(result, "hello");
}

NOLINT_TEST_F(MemoryStreamTest, Flush_Success)
{
  const auto flush_result = sut_.flush();
  EXPECT_TRUE(flush_result);
}

NOLINT_TEST_F(MemoryStreamTest, Position_Success)
{
  const auto write_result = sut_.write(hello_);
  EXPECT_TRUE(write_result);

  const auto seek_result = sut_.seek(2);
  EXPECT_TRUE(seek_result);

  const auto position_result = sut_.position();
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 2);
}

NOLINT_TEST_F(MemoryStreamTest, Seek_Success)
{
  const auto write_result = sut_.write(hello_);
  EXPECT_TRUE(write_result);

  const auto seek_result = sut_.seek(2);
  EXPECT_TRUE(seek_result);

  const auto position_result = sut_.position();
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 2);
}

NOLINT_TEST_F(MemoryStreamTest, Size_Success)
{
  const auto write_result = sut_.write(hello_);
  EXPECT_TRUE(write_result);

  const auto size_result = sut_.size();
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), buffer_.size());
}

NOLINT_TEST_F(MemoryStreamTest, Data_Success)
{
  const auto write_result = sut_.write(hello_);
  EXPECT_TRUE(write_result);

  const auto data = sut_.data();
  const std::string data_str(
    reinterpret_cast<const char*>(data.data()), data.size());
  EXPECT_EQ(data_str, "hello");
}

NOLINT_TEST_F(MemoryStreamTest, Reset_Success)
{
  const auto write_result = sut_.write(hello_);
  EXPECT_TRUE(write_result);

  sut_.reset();

  const auto position_result = sut_.position();
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 0);

  EXPECT_FALSE(sut_.eof());
}

NOLINT_TEST_F(MemoryStreamTest, Clear_Success)
{
  const auto write_result = sut_.write(hello_);
  EXPECT_TRUE(write_result);

  sut_.clear();

  const auto size_result = sut_.size();
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), buffer_.size());

  const auto position_result = sut_.position();
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 0);

  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(buffer_.data()), buffer_.size()),
    std::string(buffer_.size(), '\0'));
  EXPECT_FALSE(sut_.eof());
}

NOLINT_TEST_F(MemoryStreamTest, EOF_Success)
{
  const auto write_result = sut_.write(hello_);
  EXPECT_TRUE(write_result);

  const auto seek_result = sut_.seek(5);
  EXPECT_TRUE(seek_result);

  EXPECT_TRUE(sut_.eof());
}

NOLINT_TEST_F(MemoryStreamTest, Write_Fails_WhenSizeExceedsLimit)
{
  const auto result = sut_.write(buffer_.data(), buffer_.size() + 1);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::no_buffer_space));
}

NOLINT_TEST_F(MemoryStreamTest, Read_Fails_WhenSizeExceedsLimit)
{
  const auto result = sut_.read(buffer_.data(), buffer_.size() + 1);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

NOLINT_TEST_F(MemoryStreamTest, Seek_Fails_WhenPositionExceedsLimit)
{
  const auto result = sut_.seek(buffer_.size() + 1);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::invalid_seek));
}

NOLINT_TEST_F(MemoryStreamTest, PartialRead_Success)
{
  // Initialize with known data
  const auto write_result = sut_.write(hello_);
  EXPECT_TRUE(write_result);

  // Reset position for reading
  EXPECT_TRUE(sut_.seek(0));

  // Perform partial read
  std::array<std::byte, 2> buffer {};
  ASSERT_TRUE(sut_.read(buffer.data(), buffer.size()));
  // Verify read data
  EXPECT_EQ(std::string_view(
              reinterpret_cast<const char*>(buffer.data()), buffer.size()),
    "he");

  // Verify position after partial read
  const auto position_result = sut_.position();
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 2);

  // Verify remaining data is intact
  std::array<std::byte, 3> remaining {};
  ASSERT_TRUE(sut_.read(remaining.data(), remaining.size()));
  EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(remaining.data()),
              remaining.size()),
    "llo");
}

NOLINT_TEST_F(MemoryStreamTest, ZeroSizeOperations)
{
  std::array<std::byte, 1> buffer {};

  const auto write_result = sut_.write(buffer.data(), 0);
  EXPECT_TRUE(write_result);

  const auto read_result = sut_.read(buffer.data(), 0);
  EXPECT_TRUE(read_result);
}

NOLINT_TEST_F(MemoryStreamTest, MoveConstruction)
{
  {
    const MemoryStream moved_stream { std::move(sut_) };

    const auto size_result = moved_stream.size();
    EXPECT_TRUE(size_result);
    EXPECT_EQ(size_result.value(), buffer_.size());
  }
  EXPECT_TRUE(true);
}

NOLINT_TEST_F(MemoryStreamTest, SequentialReadWrite)
{
  std::array<std::byte, 2> read_buffer {};

  EXPECT_TRUE(sut_.write(hello_.data(), 2)); // Write "he"
  EXPECT_TRUE(sut_.write(hello_.data() + 2, 2)); // Write "ll"

  EXPECT_TRUE(sut_.seek(0));
  EXPECT_TRUE(sut_.read(read_buffer.data(), 2)); // Read "he"
  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(read_buffer.data()), 2), "he");

  EXPECT_TRUE(sut_.read(read_buffer.data(), 2)); // Read "ll"
  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(read_buffer.data()), 2), "ll");
}

NOLINT_TEST_F(MemoryStreamTest, MoveAssignment_Success)
{
  MemoryStream other_stream;
  other_stream = std::move(sut_);

  const auto size_result = other_stream.size();
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), buffer_.size());

  const auto data = other_stream.data();
  EXPECT_EQ(
    std::string_view(reinterpret_cast<const char*>(data.data()), data.size()),
    std::string_view(
      reinterpret_cast<const char*>(buffer_.data()), buffer_.size()));
}

NOLINT_TEST_F(MemoryStreamTest, InterleavedOperations_Success)
{
  constexpr std::array<std::byte, 4> write_data { 't'_b, 'e'_b, 's'_b, 't'_b };
  std::array<std::byte, 2> read_buffer {};

  EXPECT_TRUE(sut_.write(write_data.data(), 2));
  EXPECT_TRUE(sut_.seek(0));
  EXPECT_TRUE(sut_.read(read_buffer.data(), 2));
  EXPECT_TRUE(sut_.write(write_data.data() + 2, 2));

  EXPECT_TRUE(sut_.seek(0));
  EXPECT_TRUE(sut_.read(read_buffer.data(), 2));
  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(read_buffer.data()), 2), "te");
}

NOLINT_TEST_F(MemoryStreamTest, BoundaryConditions)
{
  // Fill buffer to exact capacity
  EXPECT_TRUE(sut_.write(buffer_.data(), buffer_.size()));
  std::array<std::byte, 1> x { 'x'_b };
  EXPECT_FALSE(sut_.write(x.data(), 1)); // Should fail

  // Single byte operations
  EXPECT_TRUE(sut_.seek(0));
  std::array<std::byte, 1> single_byte {};
  EXPECT_TRUE(sut_.read(single_byte.data(), 1));
}

NOLINT_TEST_F(MemoryStreamTest, DataIntegrity_MultipleOperations)
{
  const std::string test_pattern = "12345";
  std::array<std::byte, 5> test_pattern_bytes { '1'_b, '2'_b, '3'_b, '4'_b,
    '5'_b };

  // Multiple write cycles
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(sut_.seek(0));
    EXPECT_TRUE(
      sut_.write(test_pattern_bytes.data(), test_pattern_bytes.size()));

    std::array<std::byte, 5> verify_buffer {};
    EXPECT_TRUE(sut_.seek(0));
    EXPECT_TRUE(sut_.read(verify_buffer.data(), verify_buffer.size()));
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(verify_buffer.data()),
                verify_buffer.size()),
      test_pattern);
  }
}

NOLINT_TEST_F(MemoryStreamTest, Backward_Success)
{
  EXPECT_TRUE(sut_.seek(4));
  auto result = sut_.backward(2);
  EXPECT_TRUE(result);
  EXPECT_EQ(sut_.position().value(), 2);
  std::array<std::byte, 3> buffer {};
  EXPECT_TRUE(sut_.read(buffer.data(), 3));
  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(buffer.data()), 3), "cde");
}

NOLINT_TEST_F(MemoryStreamTest, Backward_Fails_BeforeBegin)
{
  EXPECT_TRUE(sut_.seek(1));
  auto result = sut_.backward(2);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

NOLINT_TEST_F(MemoryStreamTest, Forward_Success)
{
  EXPECT_TRUE(sut_.seek(0));
  auto result = sut_.forward(3);
  EXPECT_TRUE(result);
  EXPECT_EQ(sut_.position().value(), 3);
  std::array<std::byte, 2> buffer {};
  EXPECT_TRUE(sut_.read(buffer.data(), 2));
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(buffer.data()), 2), "de");
}

NOLINT_TEST_F(MemoryStreamTest, Forward_Fails_PastEnd)
{
  EXPECT_TRUE(sut_.seek(4));
  auto result = sut_.forward(10);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

NOLINT_TEST_F(MemoryStreamTest, SeekEnd_Success)
{
  auto result = sut_.seek_end();
  EXPECT_TRUE(result);
  EXPECT_EQ(sut_.position().value(), buffer_.size());
  std::array<std::byte, 1> buffer {};
  auto read_result = sut_.read(buffer.data(), 1);
  EXPECT_FALSE(read_result);
  EXPECT_EQ(read_result.error(), std::make_error_code(std::errc::io_error));
}

class InternalBufferMemoryStreamTest : public Test {
protected:
  MemoryStream sut_; // Uses internal buffer
};

NOLINT_TEST_F(InternalBufferMemoryStreamTest, Write_GrowsInternalBuffer)
{
  std::vector<std::byte> data_bytes {
    'h'_b,
    'e'_b,
    'l'_b,
    'l'_b,
    'o'_b,
    ' '_b,
    'w'_b,
    'o'_b,
    'r'_b,
    'l'_b,
    'd'_b,
  };
  const auto write_result = sut_.write(data_bytes.data(), data_bytes.size());
  EXPECT_TRUE(write_result);

  const auto size_result = sut_.size();
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), 11);
}

NOLINT_TEST_F(InternalBufferMemoryStreamTest, WriteGrowsAndMaintainsContent)
{
  std::vector<std::byte> data1_bytes {
    'h'_b,
    'e'_b,
    'l'_b,
    'l'_b,
    'o'_b,
  };
  std::vector<std::byte> data2_bytes {
    ' '_b,
    'w'_b,
    'o'_b,
    'r'_b,
    'l'_b,
    'd'_b,
  };

  EXPECT_TRUE(sut_.write(data1_bytes.data(), data1_bytes.size()));
  EXPECT_TRUE(sut_.write(data2_bytes.data(), data2_bytes.size()));

  std::vector<std::byte> buffer(11);
  EXPECT_TRUE(sut_.seek(0));
  EXPECT_TRUE(sut_.read(buffer.data(), 11));
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(buffer.data()), 11),
    "hello world");
}

NOLINT_TEST_F(InternalBufferMemoryStreamTest, StressTest_LargeData)
{
  constexpr size_t test_size = 1024ULL * 1024; // 1MB
  std::vector<std::byte> large_data(test_size, 'A'_b);

  EXPECT_TRUE(sut_.write(large_data.data(), test_size));

  std::vector<std::byte> read_buffer(test_size);
  EXPECT_TRUE(sut_.seek(0));
  EXPECT_TRUE(sut_.read(read_buffer.data(), test_size));
  EXPECT_EQ(read_buffer, large_data);
}
