//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Base/MemoryStream.h"

#include <gtest/gtest.h>

using namespace oxygen::serio;
using namespace testing;

class MemoryStreamTest : public Test
{
 protected:
  std::vector<char> buffer_ { 'a', 'b', 'c', 'd', 'e' };
  MemoryStream sut_ { std::span(buffer_) };
};

TEST_F(MemoryStreamTest, Constructor_Success)
{
  const auto size_result = sut_.size();
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), buffer_.size());

  const auto position_result = sut_.position();
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 0);

  EXPECT_FALSE(sut_.eof());
}

TEST_F(MemoryStreamTest, Write_Success)
{
  const auto write_result = sut_.write("hello", 5);
  EXPECT_TRUE(write_result);

  const auto size_result = sut_.size();
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), buffer_.size());

  const auto position_result = sut_.position();
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 5);

  EXPECT_EQ(std::string(buffer_.begin(), buffer_.end()), "hello");
}

TEST_F(MemoryStreamTest, Read_Success)
{
  const auto write_result = sut_.write("hello", 5);
  EXPECT_TRUE(write_result);

  const auto seek_result = sut_.seek(0);
  EXPECT_TRUE(seek_result);

  char buffer[5];
  const auto read_result = sut_.read(buffer, 5);
  EXPECT_TRUE(read_result);
  EXPECT_EQ(std::string(buffer, 5), "hello");
}

TEST_F(MemoryStreamTest, Flush_Success)
{
  const auto flush_result = sut_.flush();
  EXPECT_TRUE(flush_result);
}

TEST_F(MemoryStreamTest, Position_Success)
{
  const auto write_result = sut_.write("hello", 5);
  EXPECT_TRUE(write_result);

  const auto seek_result = sut_.seek(2);
  EXPECT_TRUE(seek_result);

  const auto position_result = sut_.position();
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 2);
}

TEST_F(MemoryStreamTest, Seek_Success)
{
  const auto write_result = sut_.write("hello", 5);
  EXPECT_TRUE(write_result);

  const auto seek_result = sut_.seek(2);
  EXPECT_TRUE(seek_result);

  const auto position_result = sut_.position();
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 2);
}

TEST_F(MemoryStreamTest, Size_Success)
{
  const auto write_result = sut_.write("hello", 5);
  EXPECT_TRUE(write_result);

  const auto size_result = sut_.size();
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), buffer_.size());
}

TEST_F(MemoryStreamTest, Data_Success)
{
  const auto write_result = sut_.write("hello", 5);
  EXPECT_TRUE(write_result);

  const auto data = sut_.data();
  const std::string data_str(reinterpret_cast<const char*>(data.data()), data.size());
  EXPECT_EQ(data_str, "hello");
}

TEST_F(MemoryStreamTest, Reset_Success)
{
  const auto write_result = sut_.write("hello", 5);
  EXPECT_TRUE(write_result);

  sut_.reset();

  const auto position_result = sut_.position();
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 0);

  EXPECT_FALSE(sut_.eof());
}

TEST_F(MemoryStreamTest, Clear_Success)
{
  const auto write_result = sut_.write("hello", 5);
  EXPECT_TRUE(write_result);

  sut_.clear();

  const auto size_result = sut_.size();
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), buffer_.size());

  const auto position_result = sut_.position();
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 0);

  EXPECT_EQ(std::string(buffer_.begin(), buffer_.end()), std::string(buffer_.size(), '\0'));
  EXPECT_FALSE(sut_.eof());
}

TEST_F(MemoryStreamTest, EOF_Success)
{
  const auto write_result = sut_.write("hello", 5);
  EXPECT_TRUE(write_result);

  const auto seek_result = sut_.seek(5);
  EXPECT_TRUE(seek_result);

  EXPECT_TRUE(sut_.eof());
}

TEST_F(MemoryStreamTest, Write_Fails_WhenSizeExceedsLimit)
{
  const auto result = sut_.write("data", buffer_.size() + 1);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::no_buffer_space));
}

TEST_F(MemoryStreamTest, Read_Fails_WhenSizeExceedsLimit)
{
  char buffer[1];
  const auto result = sut_.read(buffer, buffer_.size() + 1);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

TEST_F(MemoryStreamTest, Seek_Fails_WhenPositionExceedsLimit)
{
  const auto result = sut_.seek(buffer_.size() + 1);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::invalid_seek));
}

TEST_F(MemoryStreamTest, PartialRead_Success)
{
  // Initialize with known data
  const auto write_result = sut_.write("hello", 5);
  EXPECT_TRUE(write_result);

  // Reset position for reading
  EXPECT_TRUE(sut_.seek(0));

  // Perform partial read
  char buffer[2] = {}; // Initialize buffer to prevent garbage values
  const auto read_result = sut_.read(buffer, 2);
  EXPECT_TRUE(read_result);

  // Verify read data
  EXPECT_EQ(std::string_view(buffer, 2), "he");

  // Verify position after partial read
  const auto position_result = sut_.position();
  EXPECT_TRUE(position_result);
  EXPECT_EQ(position_result.value(), 2);

  // Verify remaining data is intact
  char remaining[3] = {};
  EXPECT_TRUE(sut_.read(remaining, 3));
  EXPECT_EQ(std::string_view(remaining, 3), "llo");
}

TEST_F(MemoryStreamTest, ZeroSizeOperations)
{
  const auto write_result = sut_.write("", 0);
  EXPECT_TRUE(write_result);

  char buffer[1];
  const auto read_result = sut_.read(buffer, 0);
  EXPECT_TRUE(read_result);
}

TEST_F(MemoryStreamTest, MoveConstruction)
{
  const MemoryStream moved_stream { std::move(sut_) };

  const auto size_result = moved_stream.size();
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), buffer_.size());
}

TEST_F(MemoryStreamTest, SequentialReadWrite)
{
  const std::string data = "hello";
  char read_buffer[2];

  EXPECT_TRUE(sut_.write(data.c_str(), 2)); // Write "he"
  EXPECT_TRUE(sut_.write(data.c_str() + 2, 2)); // Write "ll"

  EXPECT_TRUE(sut_.seek(0));
  EXPECT_TRUE(sut_.read(read_buffer, 2)); // Read "he"
  EXPECT_EQ(std::string(read_buffer, 2), "he");

  EXPECT_TRUE(sut_.read(read_buffer, 2)); // Read "ll"
  EXPECT_EQ(std::string(read_buffer, 2), "ll");
}

TEST_F(MemoryStreamTest, MoveAssignment_Success)
{
  MemoryStream other_stream;
  other_stream = std::move(sut_);

  const auto size_result = other_stream.size();
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), buffer_.size());

  const auto data = other_stream.data();
  EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()),
    std::string_view(buffer_.data(), buffer_.size()));
}

TEST_F(MemoryStreamTest, InterleavedOperations_Success)
{
  constexpr char write_data[] = "test";
  char read_buffer[2];

  EXPECT_TRUE(sut_.write(write_data, 2));
  EXPECT_TRUE(sut_.seek(0));
  EXPECT_TRUE(sut_.read(read_buffer, 2));
  EXPECT_TRUE(sut_.write(write_data + 2, 2));

  EXPECT_TRUE(sut_.seek(0));
  EXPECT_TRUE(sut_.read(read_buffer, 2));
  EXPECT_EQ(std::string_view(read_buffer, 2), "te");
}

TEST_F(MemoryStreamTest, BoundaryConditions)
{
  // Fill buffer to exact capacity
  EXPECT_TRUE(sut_.write(buffer_.data(), buffer_.size()));
  EXPECT_FALSE(sut_.write("x", 1)); // Should fail

  // Single byte operations
  EXPECT_TRUE(sut_.seek(0));
  char single_byte;
  EXPECT_TRUE(sut_.read(&single_byte, 1));
}

TEST_F(MemoryStreamTest, DataIntegrity_MultipleOperations)
{
  const std::string test_pattern = "12345";

  // Multiple write cycles
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(sut_.seek(0));
    EXPECT_TRUE(sut_.write(test_pattern.data(), test_pattern.size()));

    std::vector<char> verify_buffer(test_pattern.size());
    EXPECT_TRUE(sut_.seek(0));
    EXPECT_TRUE(sut_.read(verify_buffer.data(), verify_buffer.size()));
    EXPECT_EQ(std::string_view(verify_buffer.data(), verify_buffer.size()), test_pattern);
  }
}

class InternalBufferMemoryStreamTest : public Test
{
 protected:
  MemoryStream sut_; // Uses internal buffer
};

TEST_F(InternalBufferMemoryStreamTest, Write_GrowsInternalBuffer)
{
  const auto write_result = sut_.write("hello world", 11);
  EXPECT_TRUE(write_result);

  const auto size_result = sut_.size();
  EXPECT_TRUE(size_result);
  EXPECT_EQ(size_result.value(), 11);
}

TEST_F(InternalBufferMemoryStreamTest, WriteGrowsAndMaintainsContent)
{
  const std::string data1 = "hello";
  const std::string data2 = " world";

  EXPECT_TRUE(sut_.write(data1.c_str(), data1.size()));
  EXPECT_TRUE(sut_.write(data2.c_str(), data2.size()));

  char buffer[11];
  EXPECT_TRUE(sut_.seek(0));
  EXPECT_TRUE(sut_.read(buffer, 11));
  EXPECT_EQ(std::string(buffer, 11), "hello world");
}

TEST_F(InternalBufferMemoryStreamTest, StressTest_LargeData)
{
  constexpr size_t test_size = 1024ULL * 1024; // 1MB
  const std::vector large_data(test_size, 'A');

  EXPECT_TRUE(sut_.write(large_data.data(), test_size));

  std::vector<char> read_buffer(test_size);
  EXPECT_TRUE(sut_.seek(0));
  EXPECT_TRUE(sut_.read(read_buffer.data(), test_size));
  EXPECT_EQ(read_buffer, large_data);
}
