//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/FileStream.h>

#include <fstream>
#include <random>

#include <Oxygen/Testing/GTest.h>

using namespace oxygen::serio;
using namespace testing;

class MockStream {
public:
  void set_force_fail(const bool value) noexcept { force_fail_ = value; }

  void open(const std::filesystem::path&, std::ios::openmode) noexcept
  {
    is_open_ = true;
    if (force_fail_)
      fail_ = true;
    eof_ = false;
  }

  [[nodiscard]] bool is_open() const noexcept
  {
    return is_open_ && !force_fail_;
  }

  void write(const std::byte* data, size_t size)
  {
    if (force_fail_) {
      fail_ = true;
      return;
    }
    if (static_cast<size_t>(pos_) + size > buffer_.size()) {
      buffer_.resize(static_cast<size_t>(pos_) + size);
    }
    std::memcpy(buffer_.data() + pos_, data, size);
    pos_ += size;
    eof_ = false;
  }

  void read(std::byte* data, size_t size)
  {
    if (force_fail_) {
      fail_ = true;
      return;
    }
    if (static_cast<size_t>(pos_) + size > buffer_.size()) {
      fail_ = true;
      eof_ = true; // Set eofbit only when a read fails due to EOF
      return;
    }
    std::memcpy(data, buffer_.data() + pos_, size);
    pos_ += size;
    eof_ = false; // Only set after a failed read
  }

  void flush()
  {
    if (force_fail_)
      fail_ = true;
    eof_ = false;
  }

  void clear(std::ios::iostate state = std::ios::goodbit) noexcept
  {
    fail_ = (state != std::ios::goodbit);
    eof_ = false;
  }

  std::streampos tellg()
  {
    if (force_fail_) {
      fail_ = true;
      return -1;
    }
    return static_cast<std::streampos>(pos_);
  }

  void seekg(std::streamoff off, std::ios_base::seekdir way)
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

  [[nodiscard]] bool fail() const noexcept { return fail_; }
  [[nodiscard]] bool eof() const noexcept { return eof_; }

  void set_data(const std::vector<std::byte>& data) noexcept
  {
    buffer_ = data;
    pos_ = 0;
    fail_ = false;
    eof_ = false;
  }

  [[nodiscard]] std::vector<std::byte> get_data() const { return buffer_; }

private:
  std::vector<std::byte> buffer_;
  size_t pos_ { 0 };
  bool is_open_ { false };
  bool force_fail_ { false };
  bool fail_ { false };
  bool eof_ { false };
};

// Verify concept compliance
static_assert(BackingStream<MockStream>);

class FileStreamTest : public Test {
protected:
  std::filesystem::path test_path_ { "test.txt" };
  MockStream* mock_stream_ { nullptr };
  std::unique_ptr<FileStream<MockStream>> sut_;

  void SetUp() override
  {
    mock_stream_ = new MockStream();
    sut_ = std::make_unique<FileStream<MockStream>>(test_path_,
      std::ios::in | std::ios::out, std::unique_ptr<MockStream>(mock_stream_));
  }

  void TearDown() override
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

  // Helper method for large data operations
  static auto create_large_test_data(const size_t size) -> std::vector<char>
  {
    std::vector<char> data(size);
    for (size_t i = 0; i < size; ++i) {
      data[i] = static_cast<char>(i % 256);
    }
    return data;
  }
};

NOLINT_TEST_F(FileStreamTest, Constructor_Success)
{
  NOLINT_EXPECT_NO_THROW(FileStream(
    test_path_, std::ios::in | std::ios::out, std::make_unique<MockStream>()));
}

NOLINT_TEST_F(FileStreamTest, DefaultStreamTypeConstructor_Success)
{
  const auto temp_path = create_temp_file();
  NOLINT_EXPECT_NO_THROW(FileStream(temp_path, std::ios::in | std::ios::out));
  std::filesystem::remove(temp_path);
}

NOLINT_TEST_F(FileStreamTest, Write_Success)
{
  const std::vector<std::byte> bytes = { 'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b };
  const auto result = sut_->write(bytes.data(), bytes.size());
  EXPECT_TRUE(result);
  const auto data = mock_stream_->get_data();
  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(data.data()), data.size()),
    "hello");
}

NOLINT_TEST_F(FileStreamTest, Read_Success)
{
  const std::vector<std::byte> bytes = { 'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b };
  mock_stream_->set_data(bytes);
  std::vector<std::byte> buffer(5);
  const auto result = sut_->read(buffer.data(), buffer.size());
  EXPECT_TRUE(result);
  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size()),
    "hello");
}

NOLINT_TEST_F(FileStreamTest, Seek_Success)
{
  const std::vector<std::byte> bytes = { 'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b,
    ' '_b, 'w'_b, 'o'_b, 'r'_b, 'l'_b, 'd'_b };
  mock_stream_->set_data(bytes);
  const auto result = sut_->seek(6);
  EXPECT_TRUE(result);
  std::vector<std::byte> buffer(5);
  const auto read_result = sut_->read(buffer.data(), buffer.size());
  EXPECT_TRUE(read_result);
  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size()),
    "world");
}

NOLINT_TEST_F(FileStreamTest, Write_Fails_WhenSizeExceedsLimit)
{
  constexpr auto too_high = 1ULL + std::numeric_limits<std::streamsize>::max();
  std::vector<std::byte> bytes = { 'd'_b, 'a'_b, 't'_b, 'a'_b };
  const auto result = sut_->write(bytes.data(), too_high);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::invalid_argument));
}

NOLINT_TEST_F(FileStreamTest, Write_Fails_OnStreamError)
{
  mock_stream_->set_force_fail(true);
  std::vector<std::byte> bytes = { 'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b };
  const auto result = sut_->write(bytes.data(), bytes.size());
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

NOLINT_TEST_F(FileStreamTest, Read_Fails_WhenSizeExceedsLimit)
{
  std::vector<std::byte> buffer(1);
  constexpr auto too_high = 1ULL + std::numeric_limits<std::streamsize>::max();
  const auto result = sut_->read(buffer.data(), too_high);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::invalid_argument));
}

NOLINT_TEST_F(FileStreamTest, Read_Fails_OnStreamError)
{
  mock_stream_->set_force_fail(true);
  std::vector<std::byte> buffer(5);
  const auto result = sut_->read(buffer.data(), buffer.size());
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}
NOLINT_TEST_F(FileStreamTest, Size_Success)
{
  const std::vector<std::byte> bytes = { 'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b,
    ' '_b, 'w'_b, 'o'_b, 'r'_b, 'l'_b, 'd'_b };
  mock_stream_->set_data(bytes);
  auto result = sut_->size();
  EXPECT_TRUE(result);
  EXPECT_EQ(*result, 11);
}

NOLINT_TEST_F(FileStreamTest, Flush_Success)
{
  const auto result = sut_->flush();
  EXPECT_TRUE(result);
}

NOLINT_TEST_F(FileStreamTest, Position_Success)
{
  const std::vector<std::byte> bytes = { 'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b,
    ' '_b, 'w'_b, 'o'_b, 'r'_b, 'l'_b, 'd'_b };
  mock_stream_->set_data(bytes);
  const auto success = sut_->seek(6);
  EXPECT_TRUE(success);
  auto result = sut_->position();
  EXPECT_TRUE(result);
  EXPECT_EQ(*result, 6);
}

NOLINT_TEST_F(FileStreamTest, MoveOperations_Success)
{
  const auto temp_file = create_temp_file();
  const std::vector<std::byte> test_data
    = { 't'_b, 'e'_b, 's'_b, 't'_b, '_'_b, 'd'_b, 'a'_b, 't'_b, 'a'_b };

  {
    const FileStream sut(temp_file, std::ios::out);
    EXPECT_TRUE(sut.write(test_data.data(), test_data.size()));
  }

  FileStream original(temp_file);
  const FileStream moved(std::move(original));

  std::vector<std::byte> buffer(test_data.size());
  EXPECT_TRUE(moved.read(buffer.data(), buffer.size()));
  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size()),
    std::string(
      reinterpret_cast<const char*>(test_data.data()), test_data.size()));
}

NOLINT_TEST_F(FileStreamTest, LargeFileOperations_Success)
{
  const auto temp_file = create_temp_file();
  std::vector<std::byte> large_data(1024ULL * 1024);
  for (size_t i = 0; i < large_data.size(); ++i) {
    large_data[i] = static_cast<std::byte>(i % 256);
  }

  {
    const FileStream sut(temp_file, std::ios::out);
    EXPECT_TRUE(sut.write(large_data.data(), large_data.size()));
  }

  const FileStream sut(temp_file);
  std::vector<std::byte> read_buffer(large_data.size());
  EXPECT_TRUE(sut.read(read_buffer.data(), read_buffer.size()));
  EXPECT_EQ(read_buffer, large_data);
}

NOLINT_TEST_F(FileStreamTest, PartialReadWrite_Success)
{
  const auto temp_file = create_temp_file();
  const std::vector<std::byte> data = { 'h'_b, 'e'_b, 'l'_b, 'l'_b, 'o'_b,
    ' '_b, 'w'_b, 'o'_b, 'r'_b, 'l'_b, 'd'_b };

  {
    const FileStream sut(temp_file, std::ios::out);
    EXPECT_TRUE(sut.write(data.data(), data.size()));
  }

  const FileStream sut(temp_file);
  std::vector<std::byte> buffer(5);
  EXPECT_TRUE(sut.read(buffer.data(), buffer.size()));
  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size()),
    "hello");

  EXPECT_TRUE(sut.read(buffer.data(), buffer.size()));
  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size()),
    " worl");
}

NOLINT_TEST_F(FileStreamTest, EOFHandling_Success)
{
  const auto temp_file = create_temp_file();
  const std::vector<std::byte> data = { 't'_b, 'e'_b, 's'_b, 't'_b };

  {
    const FileStream sut(temp_file, std::ios::out);
    EXPECT_TRUE(sut.write(data.data(), data.size()));
  }

  const FileStream sut(temp_file);
  std::vector<std::byte> buffer(
    10, std::byte { 0 }); // Initialize buffer to known state

  // Read exactly to EOF
  EXPECT_TRUE(sut.read(buffer.data(), data.size()));
  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(buffer.data()), data.size()),
    "test");

  // Attempt read at EOF
  std::vector<std::byte> eof_buffer(1, std::byte { 0 });
  const auto eof_result = sut.read(eof_buffer.data(), 1);
  EXPECT_FALSE(eof_result);
  EXPECT_EQ(eof_buffer[0], std::byte { 0 }); // Buffer should be unchanged
}

NOLINT_TEST_F(FileStreamTest, Backward_Success)
{
  const std::vector<std::byte> data
    = { 'a'_b, 'b'_b, 'c'_b, 'd'_b, 'e'_b, 'f'_b };
  mock_stream_->set_data(data);
  ASSERT_TRUE(sut_->seek(5));
  // Move back by 2
  auto result = sut_->backward(2);
  EXPECT_TRUE(result);
  std::vector<std::byte> buffer(3, std::byte { 0 });
  EXPECT_TRUE(sut_->read(buffer.data(), buffer.size()));
  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size()),
    "def");
}

NOLINT_TEST_F(FileStreamTest, Backward_Fails_BeforeBegin)
{
  const std::vector<std::byte> data
    = { 'a'_b, 'b'_b, 'c'_b, 'd'_b, 'e'_b, 'f'_b };
  mock_stream_->set_data(data);
  ASSERT_TRUE(sut_->seek(1));
  // Try to move back by 2 (should fail)
  auto result = sut_->backward(2);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

NOLINT_TEST_F(FileStreamTest, Forward_Success)
{
  const std::vector<std::byte> data
    = { 'a'_b, 'b'_b, 'c'_b, 'd'_b, 'e'_b, 'f'_b };
  mock_stream_->set_data(data);
  ASSERT_TRUE(sut_->seek(0));
  // Move forward by 3
  auto result = sut_->forward(3);
  EXPECT_TRUE(result);
  std::vector<std::byte> buffer(3, std::byte { 0 });
  EXPECT_TRUE(sut_->read(buffer.data(), buffer.size()));
  EXPECT_EQ(
    std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size()),
    "def");
}

NOLINT_TEST_F(FileStreamTest, Forward_Fails_PastEnd)
{
  const std::vector<std::byte> data
    = { 'a'_b, 'b'_b, 'c'_b, 'd'_b, 'e'_b, 'f'_b };
  mock_stream_->set_data(data);
  ASSERT_TRUE(sut_->seek(4));
  // Try to move forward by 10 (should fail)
  auto result = sut_->forward(10);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

NOLINT_TEST_F(FileStreamTest, SeekEnd_Success)
{
  const std::vector<std::byte> data
    = { 'a'_b, 'b'_b, 'c'_b, 'd'_b, 'e'_b, 'f'_b };
  mock_stream_->set_data(data);
  auto result = sut_->seek_end();
  EXPECT_TRUE(result);
  // Should be at end, so reading should fail with no_buffer_space
  std::vector<std::byte> buffer(1, std::byte { 0 });
  auto read_result = sut_->read(buffer.data(), 1);
  EXPECT_FALSE(read_result);
  EXPECT_EQ(
    read_result.error(), std::make_error_code(std::errc::no_buffer_space));
}

NOLINT_TEST_F(FileStreamTest, SeekEnd_Fails_OnStreamError)
{
  mock_stream_->set_force_fail(true);
  auto result = sut_->seek_end();
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}
