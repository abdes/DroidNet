//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <fstream>
#include <random>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Serio/FileStream.h>

using oxygen::serio::BackingStream;
using oxygen::serio::FileStream;

namespace {

class MockBackingStream {
public:
  // ReSharper disable CppDeclaratorNeverUsed

  auto set_force_fail(const bool value) noexcept -> void
  {
    force_fail_ = value;
  }

  auto open(const std::filesystem::path&, std::ios::openmode) noexcept -> void
  {
    is_open_ = true;
    if (force_fail_) {
      fail_ = true;
    }
    eof_ = false;
  }

  auto close() noexcept -> void
  {
    is_open_ = false;
    clear();
  }

  [[nodiscard]] auto is_open() const noexcept -> bool
  {
    return is_open_ && !force_fail_;
  }

  auto write(const std::byte* data, const size_t size) -> void
  {
    if (force_fail_) {
      fail_ = true;
      return;
    }
    if (pos_ + size > buffer_.size()) {
      buffer_.resize(pos_ + size);
    }
    std::memcpy(buffer_.data() + pos_, data, size);
    pos_ += size;
    eof_ = false;
  }

  auto read(std::byte* data, const size_t size) -> void
  {
    if (force_fail_) {
      fail_ = true;
      return;
    }
    if (pos_ + size > buffer_.size()) {
      fail_ = true;
      eof_ = true; // Set eof bit only when a read fails due to EOF
      return;
    }
    std::memcpy(data, buffer_.data() + pos_, size);
    pos_ += size;
    eof_ = false; // Only set after a failed read
  }

  auto flush() -> void
  {
    if (force_fail_) {
      fail_ = true;
    }
    eof_ = false;
  }

  auto clear(const std::ios::iostate state = std::ios::goodbit) noexcept -> void
  {
    fail_ = (state != std::ios::goodbit);
    eof_ = false;
  }

  auto tellg() -> std::streampos
  {
    if (force_fail_) {
      fail_ = true;
      return -1;
    }
    return static_cast<std::streamoff>(pos_);
  }

  auto seekg(const std::streamoff off, const std::ios_base::seekdir way) -> void
  {
    if (force_fail_) {
      fail_ = true;
      return;
    }
    size_t new_pos = pos_;
    if (way == std::ios::beg) {
      new_pos = static_cast<size_t>(off);
    } else if (way == std::ios::cur) {
      new_pos = static_cast<size_t>(static_cast<std::streamoff>(pos_) + off);
    } else if (way == std::ios::end) {
      new_pos = static_cast<size_t>(
        static_cast<std::streamoff>(buffer_.size()) + off);
    }
    if (new_pos > buffer_.size()) {
      fail_ = true;
      eof_ = true;
      return;
    }
    pos_ = new_pos;
    eof_ = false;
  }

  [[nodiscard]] auto fail() const noexcept -> bool { return fail_; }
  [[nodiscard]] auto eof() const noexcept -> bool { return eof_; }

  auto set_data(const std::vector<std::byte>& data) noexcept -> void
  {
    buffer_ = data;
    pos_ = 0;
    fail_ = false;
    eof_ = false;
  }

  [[nodiscard]] auto get_data() const -> std::vector<std::byte>
  {
    return buffer_;
  }

  // ReSharper enable CppDeclaratorNeverUsed

private:
  std::vector<std::byte> buffer_;
  size_t pos_ { 0 };
  bool is_open_ { false };
  bool force_fail_ { false };
  bool fail_ { false };
  bool eof_ { false };
};
static_assert(BackingStream<MockBackingStream>);

using TestFileStream = FileStream<MockBackingStream>;

class FileStreamBasicTest : public testing::Test {
protected:
  std::filesystem::path test_path_ { "test.txt" };
  MockBackingStream* mock_stream_ { nullptr };
  std::unique_ptr<TestFileStream> sut_;

  auto SetUp() -> void override
  {
    mock_stream_ = new MockBackingStream();
    sut_ = std::make_unique<FileStream<MockBackingStream>>(test_path_,
      std::ios::in | std::ios::out,
      std::unique_ptr<MockBackingStream>(mock_stream_));
  }

  auto TearDown() -> void override
  {
    // mock_stream_ will be auto-deleted by the sut_;
  }

  [[nodiscard]] static auto create_temp_file() -> std::filesystem::path
  {
    const auto temp_dir = std::filesystem::temp_directory_path();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution dis(0, 999999);
    std::filesystem::path temp_path;
    do {
      temp_path = temp_dir / ("temp_file_" + std::to_string(dis(gen)));
    } while (exists(temp_path));
    std::ofstream(temp_path).close();
    return temp_path;
  }
};

//=== FileStream Basic Tests ===----------------------------------------------//

//! Tests that the FileStream constructor succeeds with valid arguments.
NOLINT_TEST_F(FileStreamBasicTest, Constructor_Success)
{
  // Arrange
  // (No setup needed, uses test_path_ and default args)

  // Act & Assert
  NOLINT_EXPECT_NO_THROW(FileStream(test_path_, std::ios::in | std::ios::out,
    std::make_unique<MockBackingStream>()));
}

//! Tests that the FileStream constructor succeeds with the default stream type.
NOLINT_TEST_F(FileStreamBasicTest, DefaultStreamTypeConstructor_Success)
{
  // Arrange
  const auto temp_path = create_temp_file();

  // Act & Assert
  NOLINT_EXPECT_NO_THROW(FileStream(temp_path, std::ios::in | std::ios::out));

  // Cleanup
  std::filesystem::remove(temp_path);
}

//! Tests that Write succeeds for a valid input buffer.
NOLINT_TEST_F(FileStreamBasicTest, Write_Success)
{
  // Arrange
  const std::vector bytes = { 'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b };

  // Act
  const auto result = sut_->Write(bytes.data(), bytes.size());

  // Assert
  EXPECT_TRUE(result);
  const auto data = mock_stream_->get_data();
  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(data.data()), data.size()),
    "hello");
}

//! Tests that Read succeeds for a valid input buffer.
NOLINT_TEST_F(FileStreamBasicTest, Read_Success)
{
  // Arrange
  const std::vector bytes = { 'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b };
  mock_stream_->set_data(bytes);
  std::vector<std::byte> buffer(5);

  // Act
  const auto result = sut_->Read(buffer.data(), buffer.size());

  // Assert
  EXPECT_TRUE(result);
  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size()),
    "hello");
}

//! Tests that Seek moves the stream position and subsequent Read returns
//! expected data.
NOLINT_TEST_F(FileStreamBasicTest, Seek_Success)
{
  // Arrange
  const std::vector bytes = { 'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b, ' '_b, 'w'_b,
    'o'_b, 'r'_b, 'l'_b, 'd'_b };
  mock_stream_->set_data(bytes);

  // Act
  const auto result = sut_->Seek(6);

  // Assert
  EXPECT_TRUE(result);
  std::vector<std::byte> buffer(5);
  const auto read_result = sut_->Read(buffer.data(), buffer.size());
  EXPECT_TRUE(read_result);
  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size()),
    "world");
}

//! Tests that Size returns the correct size of the stream.
NOLINT_TEST_F(FileStreamBasicTest, Size_Success)
{
  // Arrange
  const std::vector bytes = { 'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b, ' '_b, 'w'_b,
    'o'_b, 'r'_b, 'l'_b, 'd'_b };
  mock_stream_->set_data(bytes);

  // Act
  auto result = sut_->Size();

  // Assert
  EXPECT_TRUE(result);
  EXPECT_EQ(*result, 11);
}

//! Tests that Flush succeeds on the stream.
NOLINT_TEST_F(FileStreamBasicTest, Flush_Success)
{
  // Arrange
  // (No setup needed)

  // Act
  const auto result = sut_->Flush();

  // Assert
  EXPECT_TRUE(result);
}

//! Tests that Position returns the correct current position in the stream.
NOLINT_TEST_F(FileStreamBasicTest, Position_Success)
{
  // Arrange
  const std::vector bytes = { 'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b, ' '_b, 'w'_b,
    'o'_b, 'r'_b, 'l'_b, 'd'_b };
  mock_stream_->set_data(bytes);
  const auto success = sut_->Seek(6);

  // Act
  auto result = sut_->Position();

  // Assert
  EXPECT_TRUE(success);
  EXPECT_TRUE(result);
  EXPECT_EQ(*result, 6);
}

//! Tests that move construction and move assignment work for FileStream.
NOLINT_TEST_F(FileStreamBasicTest, MoveOperations_Success)
{
  // Arrange
  const auto temp_file = create_temp_file();
  const std::vector test_data
    = { 't'_b, 'e'_b, 's'_b, 't'_b, '_'_b, 'd'_b, 'a'_b, 't'_b, 'a'_b };

  // Act
  {
    FileStream sut(temp_file, std::ios::out);
    EXPECT_TRUE(sut.Write(test_data.data(), test_data.size()));
  }
  FileStream original(temp_file);
  FileStream moved(std::move(original));
  std::vector<std::byte> buffer(test_data.size());
  const auto read_result = moved.Read(buffer.data(), buffer.size());

  // Assert
  EXPECT_TRUE(read_result);
  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size()),
    std::string(
      reinterpret_cast<const char*>(test_data.data()), test_data.size()));
}

//! Tests that large file operations (write/read) succeed for FileStream.
NOLINT_TEST_F(FileStreamBasicTest, LargeFileOperations_Success)
{
  // Arrange
  const auto temp_file = create_temp_file();
  std::vector<std::byte> large_data(1024ULL * 1024);
  for (size_t i = 0; i < large_data.size(); ++i) {
    large_data[i] = static_cast<std::byte>(i % 256);
  }

  // Act
  {
    FileStream sut(temp_file, std::ios::out);
    EXPECT_TRUE(sut.Write(large_data.data(), large_data.size()));
  }
  FileStream sut(temp_file);
  std::vector<std::byte> read_buffer(large_data.size());
  const auto read_result = sut.Read(read_buffer.data(), read_buffer.size());

  // Assert
  EXPECT_TRUE(read_result);
  EXPECT_EQ(read_buffer, large_data);
}

//! Tests that partial read and write operations succeed for FileStream.
NOLINT_TEST_F(FileStreamBasicTest, PartialReadWrite_Success)
{
  // Arrange
  const auto temp_file = create_temp_file();
  const std::vector data = { 'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b, ' '_b, 'w'_b,
    'o'_b, 'r'_b, 'l'_b, 'd'_b };

  // Act
  {
    FileStream sut(temp_file, std::ios::out);
    EXPECT_TRUE(sut.Write(data.data(), data.size()));
  }
  FileStream sut(temp_file);
  std::vector buffer(5, std::byte { 0 });
  auto read1 = sut.Read(buffer.data(), buffer.size());
  std::string first(
    reinterpret_cast<const char*>(buffer.data()), buffer.size());
  auto read2 = sut.Read(buffer.data(), buffer.size());
  std::string second(
    reinterpret_cast<const char*>(buffer.data()), buffer.size());

  // Assert
  EXPECT_TRUE(read1);
  EXPECT_EQ(first, "hello");
  EXPECT_TRUE(read2);
  // ReSharper disable once StringLiteralTypo
  EXPECT_EQ(second, " worl");
}

//! Tests correct EOF handling: reading to EOF and attempting to read past EOF.
NOLINT_TEST_F(FileStreamBasicTest, EOFHandling_Success)
{
  // Arrange
  const auto temp_file = create_temp_file();
  const std::vector data = { 't'_b, 'e'_b, 's'_b, 't'_b };

  // Act
  {
    FileStream sut(temp_file, std::ios::out);
    EXPECT_TRUE(sut.Write(data.data(), data.size()));
  }
  FileStream sut(temp_file);
  std::vector buffer(10, std::byte { 0 });
  auto read1 = sut.Read(buffer.data(), data.size());
  std::string first(reinterpret_cast<const char*>(buffer.data()), data.size());
  std::vector eof_buffer(1, std::byte { 0 });
  auto eof_result = sut.Read(eof_buffer.data(), 1);

  // Assert
  EXPECT_TRUE(read1);
  EXPECT_EQ(first, "test");
  EXPECT_FALSE(eof_result);
  EXPECT_EQ(eof_buffer[0], std::byte { 0 }); // Buffer should be unchanged
}

//! Tests that Backward moves the stream position backward and subsequent Read
//! returns expected data.
NOLINT_TEST_F(FileStreamBasicTest, Backward_Success)
{
  // Arrange
  const std::vector data = { 'a'_b, 'b'_b, 'c'_b, 'd'_b, 'e'_b, 'f'_b };
  mock_stream_->set_data(data);
  ASSERT_TRUE(sut_->Seek(5));

  // Act
  const auto result = sut_->Backward(2);
  std::vector buffer(3, std::byte { 0 });
  const auto read_result = sut_->Read(buffer.data(), buffer.size());
  const std::string read_str(
    reinterpret_cast<const char*>(buffer.data()), buffer.size());

  // Assert
  EXPECT_TRUE(result);
  EXPECT_TRUE(read_result);
  EXPECT_EQ(read_str, "def");
}

//! Tests that Forward moves the stream position forward and subsequent Read
//! returns expected data.
NOLINT_TEST_F(FileStreamBasicTest, Forward_Success)
{
  // Arrange
  const std::vector data = { 'a'_b, 'b'_b, 'c'_b, 'd'_b, 'e'_b, 'f'_b };
  mock_stream_->set_data(data);
  ASSERT_TRUE(sut_->Seek(0));

  // Act
  const auto result = sut_->Forward(3);
  std::vector buffer(3, std::byte { 0 });
  const auto read_result = sut_->Read(buffer.data(), buffer.size());
  const std::string read_str(
    reinterpret_cast<const char*>(buffer.data()), buffer.size());

  // Assert
  EXPECT_TRUE(result);
  EXPECT_TRUE(read_result);
  EXPECT_EQ(read_str, "def");
}

//! Tests that SeekEnd moves the stream to the end and subsequent Read fails as
//! expected.
NOLINT_TEST_F(FileStreamBasicTest, SeekEnd_Success)
{
  // Arrange
  const std::vector data = { 'a'_b, 'b'_b, 'c'_b, 'd'_b, 'e'_b, 'f'_b };
  mock_stream_->set_data(data);

  // Act
  const auto result = sut_->SeekEnd();
  std::vector buffer(1, std::byte { 0 });
  const auto read_result = sut_->Read(buffer.data(), 1);

  // Assert
  EXPECT_TRUE(result);
  EXPECT_FALSE(read_result);
  EXPECT_EQ(
    read_result.error(), std::make_error_code(std::errc::no_buffer_space));
}

//=== FileStream Error Tests ===----------------------------------------------//

//! Error scenario tests for FileStream operations.
class FileStreamErrorTest : public FileStreamBasicTest { };

//! Tests that Write fails when size exceeds maximum allowed.
NOLINT_TEST_F(FileStreamErrorTest, Write_Fails_WhenSizeExceedsLimit)
{
  // Arrange
  constexpr auto too_high = 1ULL + std::numeric_limits<std::streamsize>::max();
  const std::vector bytes = { 'd'_b, 'a'_b, 't'_b, 'a'_b };

  // Act
  const auto result = sut_->Write(bytes.data(), too_high);

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::invalid_argument));
}

//! Tests that Write fails when the underlying stream is in a forced failure
//! state.
NOLINT_TEST_F(FileStreamErrorTest, Write_Fails_OnStreamError)
{
  // Arrange
  mock_stream_->set_force_fail(true);
  const std::vector bytes = { 'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b };

  // Act
  const auto result = sut_->Write(bytes.data(), bytes.size());

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

//! Tests that Read fails when size exceeds maximum allowed.
NOLINT_TEST_F(FileStreamErrorTest, Read_Fails_WhenSizeExceedsLimit)
{
  // Arrange
  std::vector<std::byte> buffer(1);
  constexpr auto too_high = 1ULL + std::numeric_limits<std::streamsize>::max();

  // Act
  const auto result = sut_->Read(buffer.data(), too_high);

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::invalid_argument));
}

//! Tests that Read fails when the underlying stream is in a forced failure
//! state.
NOLINT_TEST_F(FileStreamErrorTest, Read_Fails_OnStreamError)
{
  // Arrange
  mock_stream_->set_force_fail(true);
  std::vector<std::byte> buffer(5);

  // Act
  const auto result = sut_->Read(buffer.data(), buffer.size());

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

//! Tests that Backward fails when moving before the beginning of the stream.
NOLINT_TEST_F(FileStreamErrorTest, Backward_Fails_BeforeBegin)
{
  // Arrange
  const std::vector data = { 'a'_b, 'b'_b, 'c'_b, 'd'_b, 'e'_b, 'f'_b };
  mock_stream_->set_data(data);
  ASSERT_TRUE(sut_->Seek(1));

  // Act
  const auto result = sut_->Backward(2);

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

//! Tests that Forward fails when moving past the end of the stream.
NOLINT_TEST_F(FileStreamErrorTest, Forward_Fails_PastEnd)
{
  // Arrange
  const std::vector data = { 'a'_b, 'b'_b, 'c'_b, 'd'_b, 'e'_b, 'f'_b };
  mock_stream_->set_data(data);
  ASSERT_TRUE(sut_->Seek(4));

  // Act
  const auto result = sut_->Forward(10);

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

//! Tests that SeekEnd fails when the underlying stream is in a forced failure
//! state.
NOLINT_TEST_F(FileStreamErrorTest, SeekEnd_Fails_OnStreamError)
{
  // Arrange
  mock_stream_->set_force_fail(true);

  // Act
  const auto result = sut_->SeekEnd();

  // Assert
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

} // namespace
