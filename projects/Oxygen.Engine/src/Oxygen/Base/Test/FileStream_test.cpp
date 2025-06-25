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

  void open(
    const std::filesystem::path& /*path*/, std::ios::openmode /*mode*/) noexcept
  {
    is_open_ = true;
    if (force_fail_) {
      stream_.setstate(std::ios::failbit);
    }
  }

  [[nodiscard]] auto is_open() const noexcept -> bool
  {
    return is_open_ && !force_fail_;
  }

  auto write(const char* data, const std::streamsize size)
    -> std::basic_ostream<char>&
  {
    if (force_fail_) {
      stream_.setstate(std::ios::failbit);
    }
    return stream_.write(data, size);
  }

  auto read(char* data, const std::streamsize size) -> std::basic_istream<char>&
  {
    if (force_fail_) {
      stream_.setstate(std::ios::failbit);
    }
    return stream_.read(data, size);
  }

  auto flush() -> std::basic_ostream<char>&
  {
    if (force_fail_) {
      stream_.setstate(std::ios::failbit);
    }
    return stream_.flush();
  }

  [[nodiscard]] auto tellg() -> std::streampos
  {
    if (force_fail_) {
      stream_.setstate(std::ios::failbit);
      return { -1 };
    }
    return stream_.tellg();
  }

  auto seekg(const std::streamoff off, const std::ios_base::seekdir way)
    -> std::basic_istream<char>&
  {
    if (force_fail_) {
      stream_.setstate(std::ios::failbit);
    }
    return stream_.seekg(off, way);
  }

  [[nodiscard]] auto fail() const noexcept -> bool { return stream_.fail(); }

  [[nodiscard]] auto eof() const noexcept -> bool { return stream_.eof(); }

  void clear() noexcept { stream_.clear(); }

  void set_data(const std::string& data) noexcept { stream_.str(data); }

  [[nodiscard]] auto get_data() const -> std::string { return stream_.str(); }

private:
  std::stringstream stream_;
  bool is_open_ { false };
  bool force_fail_ { false };
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
  const auto result = sut_->write("hello", 5);
  EXPECT_TRUE(result);
  EXPECT_EQ(mock_stream_->get_data(), "hello");
}

NOLINT_TEST_F(FileStreamTest, Read_Success)
{
  mock_stream_->set_data("hello");
  char buffer[5];
  const auto result = sut_->read(buffer, 5);
  EXPECT_TRUE(result);
  EXPECT_EQ(std::string(buffer, 5), "hello");
}

NOLINT_TEST_F(FileStreamTest, Seek_Success)
{
  mock_stream_->set_data("hello world");
  const auto result = sut_->seek(6);
  EXPECT_TRUE(result);
  char buffer[5];
  const auto read_result = sut_->read(buffer, 5);
  EXPECT_TRUE(read_result);
  EXPECT_EQ(std::string(buffer, 5), "world");
}

NOLINT_TEST_F(FileStreamTest, Write_Fails_WhenSizeExceedsLimit)
{
  constexpr auto too_high = 1ULL + std::numeric_limits<std::streamsize>::max();
  const auto result = sut_->write("data", too_high);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::invalid_argument));
}

NOLINT_TEST_F(FileStreamTest, Write_Fails_OnStreamError)
{
  mock_stream_->set_force_fail(true);
  const auto result = sut_->write("hello", 5);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

NOLINT_TEST_F(FileStreamTest, Read_Fails_WhenSizeExceedsLimit)
{
  char buffer[1];
  constexpr auto too_high = 1ULL + std::numeric_limits<std::streamsize>::max();
  const auto result = sut_->read(buffer, too_high);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::invalid_argument));
}

NOLINT_TEST_F(FileStreamTest, Read_Fails_OnStreamError)
{
  mock_stream_->set_force_fail(true);
  char buffer[5];
  const auto result = sut_->read(buffer, 5);
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

NOLINT_TEST_F(FileStreamTest, Size_Success)
{
  mock_stream_->set_data("hello world");
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
  mock_stream_->set_data("hello world");
  const auto success = sut_->seek(6);
  EXPECT_TRUE(success);
  auto result = sut_->position();
  EXPECT_TRUE(result);
  EXPECT_EQ(*result, 6);
}

NOLINT_TEST_F(FileStreamTest, Flush_Fails_OnStreamError)
{
  mock_stream_->set_force_fail(true);
  const auto result = sut_->flush();
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

NOLINT_TEST_F(FileStreamTest, Position_Fails_OnStreamError)
{
  mock_stream_->set_force_fail(true);
  const auto result = sut_->position();
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error(), std::make_error_code(std::errc::io_error));
}

NOLINT_TEST_F(FileStreamTest, MoveOperations_Success)
{
  const auto temp_file = create_temp_file();
  const std::string test_data = "test_data";

  {
    const FileStream sut(temp_file, std::ios::out);
    EXPECT_TRUE(sut.write(test_data.data(), test_data.size()));
  }

  FileStream original(temp_file);
  const FileStream moved(std::move(original));

  char buffer[9];
  EXPECT_TRUE(moved.read(buffer, test_data.size()));
  EXPECT_EQ(std::string_view(buffer, test_data.size()), test_data);
}

NOLINT_TEST_F(FileStreamTest, LargeFileOperations_Success)
{
  const auto temp_file = create_temp_file();
  const auto large_data = create_large_test_data(1024ULL * 1024); // 1MB

  {
    const FileStream sut(temp_file, std::ios::out);
    EXPECT_TRUE(sut.write(large_data.data(), large_data.size()));
  }

  const FileStream sut(temp_file);
  std::vector<char> read_buffer(large_data.size());
  EXPECT_TRUE(sut.read(read_buffer.data(), read_buffer.size()));
  EXPECT_EQ(read_buffer, large_data);
}

NOLINT_TEST_F(FileStreamTest, PartialReadWrite_Success)
{
  const auto temp_file = create_temp_file();
  const std::string data = "hello world";

  {
    const FileStream sut(temp_file, std::ios::out);
    EXPECT_TRUE(sut.write(data.data(), data.size()));
  }

  const FileStream sut(temp_file);
  char buffer[5];
  EXPECT_TRUE(sut.read(buffer, 5));
  EXPECT_EQ(std::string_view(buffer, 5), "hello");

  EXPECT_TRUE(sut.read(buffer, 5));
  // ReSharper disable once StringLiteralTypo
  EXPECT_EQ(std::string_view(buffer, 5), " worl");
}

NOLINT_TEST_F(FileStreamTest, EOFHandling_Success)
{
  const auto temp_file = create_temp_file();
  const std::string data = "test";

  {
    const FileStream sut(temp_file, std::ios::out);
    EXPECT_TRUE(sut.write(data.data(), data.size()));
  }

  const FileStream sut(temp_file);
  char buffer[10] = {}; // Initialize buffer to known state

  // Read exactly to EOF
  EXPECT_TRUE(sut.read(buffer, data.size()));
  EXPECT_EQ(std::string_view(buffer, data.size()), data);

  // Attempt read at EOF
  char eof_buffer[1] = {};
  const auto eof_result = sut.read(eof_buffer, 1);
  EXPECT_FALSE(eof_result);
  EXPECT_EQ(eof_buffer[0], 0); // Buffer should be unchanged
}
