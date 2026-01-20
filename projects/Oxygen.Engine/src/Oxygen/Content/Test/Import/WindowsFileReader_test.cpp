//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/WindowsFileReader.h>

#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Testing/GTest.h>

#include <filesystem>
#include <fstream>

using namespace oxygen::content::import;
using namespace oxygen::co;
namespace co = oxygen::co;

namespace {

//! Test fixture with temporary file creation.
class WindowsFileReaderTest : public ::testing::Test {
protected:
  auto SetUp() -> void override
  {
    loop_ = std::make_unique<ImportEventLoop>();
    reader_ = std::make_unique<WindowsFileReader>(*loop_);
    test_dir_
      = std::filesystem::temp_directory_path() / "oxygen_file_reader_test";
    std::filesystem::create_directories(test_dir_);
  }

  auto TearDown() -> void override
  {
    reader_.reset();
    loop_.reset();
    std::error_code ec;
    std::filesystem::remove_all(test_dir_, ec);
  }

  //! Create a test file with specified content.
  auto CreateTestFile(std::string_view name, std::span<const std::byte> content)
    -> std::filesystem::path
  {
    auto path = test_dir_ / name;
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(content.data()),
      static_cast<std::streamsize>(content.size()));
    return path;
  }

  //! Create a test file with string content.
  auto CreateTestFile(std::string_view name, std::string_view content)
    -> std::filesystem::path
  {
    auto path = test_dir_ / name;
    std::ofstream file(path, std::ios::binary);
    file << content;
    return path;
  }

  std::unique_ptr<ImportEventLoop> loop_;
  std::unique_ptr<WindowsFileReader> reader_;
  std::filesystem::path test_dir_;
};

//=== ReadFile Tests ===------------------------------------------------------//

//! Verify reading an entire small file.
NOLINT_TEST_F(WindowsFileReaderTest, ReadFile_SmallFile_ReadsAllContent)
{
  // Arrange
  const std::string content = "Hello, World!";
  auto path = CreateTestFile("small.txt", content);

  // Act
  std::vector<std::byte> result;
  co::Run(*loop_, [&]() -> Co<> {
    auto read_result = co_await reader_->ReadFile(path);
    EXPECT_TRUE(read_result.has_value());
    result = std::move(read_result).value();
  });

  // Assert
  EXPECT_EQ(result.size(), content.size());
  std::string result_str(
    reinterpret_cast<const char*>(result.data()), result.size());
  EXPECT_EQ(result_str, content);
}

//! Verify reading a larger file (multiple KB).
NOLINT_TEST_F(WindowsFileReaderTest, ReadFile_LargerFile_ReadsAllContent)
{
  // Arrange
  std::string content(64 * 1024, 'X'); // 64KB
  for (size_t i = 0; i < content.size(); ++i) {
    content[i] = static_cast<char>('A' + (i % 26));
  }
  auto path = CreateTestFile("larger.bin", content);

  // Act
  std::vector<std::byte> result;
  co::Run(*loop_, [&]() -> Co<> {
    auto read_result = co_await reader_->ReadFile(path);
    EXPECT_TRUE(read_result.has_value());
    result = std::move(read_result).value();
  });

  // Assert
  EXPECT_EQ(result.size(), content.size());
  std::string result_str(
    reinterpret_cast<const char*>(result.data()), result.size());
  EXPECT_EQ(result_str, content);
}

//! Verify reading with an offset.
NOLINT_TEST_F(WindowsFileReaderTest, ReadFile_WithOffset_ReadsFromOffset)
{
  // Arrange
  const std::string content = "Hello, World!";
  auto path = CreateTestFile("offset.txt", content);

  // Act
  std::vector<std::byte> result;
  co::Run(*loop_, [&]() -> Co<> {
    ReadOptions options;
    options.offset = 7; // Skip "Hello, "
    auto read_result = co_await reader_->ReadFile(path, options);
    EXPECT_TRUE(read_result.has_value());
    result = std::move(read_result).value();
  });

  // Assert
  EXPECT_EQ(result.size(), 6u); // "World!"
  std::string result_str(
    reinterpret_cast<const char*>(result.data()), result.size());
  EXPECT_EQ(result_str, "World!");
}

//! Verify reading with max_bytes limit.
NOLINT_TEST_F(WindowsFileReaderTest, ReadFile_WithMaxBytes_LimitsRead)
{
  // Arrange
  const std::string content = "Hello, World!";
  auto path = CreateTestFile("limited.txt", content);

  // Act
  std::vector<std::byte> result;
  co::Run(*loop_, [&]() -> Co<> {
    ReadOptions options;
    options.max_bytes = 5; // Only read "Hello"
    auto read_result = co_await reader_->ReadFile(path, options);
    EXPECT_TRUE(read_result.has_value());
    result = std::move(read_result).value();
  });

  // Assert
  EXPECT_EQ(result.size(), 5u);
  std::string result_str(
    reinterpret_cast<const char*>(result.data()), result.size());
  EXPECT_EQ(result_str, "Hello");
}

//! Verify reading with offset and max_bytes.
NOLINT_TEST_F(WindowsFileReaderTest, ReadFile_WithOffsetAndMaxBytes_Works)
{
  // Arrange
  const std::string content = "Hello, World!";
  auto path = CreateTestFile("combo.txt", content);

  // Act
  std::vector<std::byte> result;
  co::Run(*loop_, [&]() -> Co<> {
    ReadOptions options;
    options.offset = 7;
    options.max_bytes = 5; // "World" without "!"
    auto read_result = co_await reader_->ReadFile(path, options);
    EXPECT_TRUE(read_result.has_value());
    result = std::move(read_result).value();
  });

  // Assert
  EXPECT_EQ(result.size(), 5u);
  std::string result_str(
    reinterpret_cast<const char*>(result.data()), result.size());
  EXPECT_EQ(result_str, "World");
}

//! Verify reading non-existent file returns error.
NOLINT_TEST_F(WindowsFileReaderTest, ReadFile_NonExistent_ReturnsError)
{
  // Arrange
  auto path = test_dir_ / "nonexistent.txt";

  // Act
  FileError error = FileError::kOk;
  co::Run(*loop_, [&]() -> Co<> {
    auto read_result = co_await reader_->ReadFile(path);
    EXPECT_TRUE(read_result.has_error());
    error = read_result.error().code;
  });

  // Assert
  EXPECT_EQ(error, FileError::kNotFound);
}

//! Verify reading with offset past EOF returns empty buffer.
NOLINT_TEST_F(WindowsFileReaderTest, ReadFile_OffsetPastEOF_ReturnsEmpty)
{
  // Arrange
  const std::string content = "Hello";
  auto path = CreateTestFile("short.txt", content);

  // Act
  std::vector<std::byte> result;
  co::Run(*loop_, [&]() -> Co<> {
    ReadOptions options;
    options.offset = 100; // Past EOF
    auto read_result = co_await reader_->ReadFile(path, options);
    EXPECT_TRUE(read_result.has_value());
    result = std::move(read_result).value();
  });

  // Assert
  EXPECT_TRUE(result.empty());
}

//=== GetFileInfo Tests ===---------------------------------------------------//

//! Verify getting file info for existing file.
NOLINT_TEST_F(WindowsFileReaderTest, GetFileInfo_ExistingFile_ReturnsInfo)
{
  // Arrange
  const std::string content = "Test content";
  auto path = CreateTestFile("info.txt", content);

  // Act
  FileInfo info {};
  co::Run(*loop_, [&]() -> Co<> {
    auto result = co_await reader_->GetFileInfo(path);
    EXPECT_TRUE(result.has_value());
    info = result.value();
  });

  // Assert
  EXPECT_EQ(info.size, content.size());
  EXPECT_FALSE(info.is_directory);
  EXPECT_FALSE(info.is_symlink);
}

//! Verify getting file info for directory.
NOLINT_TEST_F(WindowsFileReaderTest, GetFileInfo_Directory_ReturnsInfo)
{
  // Arrange - use test_dir_ which already exists

  // Act
  FileInfo info {};
  co::Run(*loop_, [&]() -> Co<> {
    auto result = co_await reader_->GetFileInfo(test_dir_);
    EXPECT_TRUE(result.has_value());
    info = result.value();
  });

  // Assert
  EXPECT_TRUE(info.is_directory);
}

//! Verify getting file info for non-existent returns error.
NOLINT_TEST_F(WindowsFileReaderTest, GetFileInfo_NonExistent_ReturnsError)
{
  // Arrange
  auto path = test_dir_ / "nonexistent.txt";

  // Act
  FileError error = FileError::kOk;
  co::Run(*loop_, [&]() -> Co<> {
    auto result = co_await reader_->GetFileInfo(path);
    EXPECT_TRUE(result.has_error());
    error = result.error().code;
  });

  // Assert
  EXPECT_EQ(error, FileError::kNotFound);
}

//=== Exists Tests ===--------------------------------------------------------//

//! Verify Exists returns true for existing file.
NOLINT_TEST_F(WindowsFileReaderTest, Exists_ExistingFile_ReturnsTrue)
{
  // Arrange
  auto path = CreateTestFile("exists.txt", "content");

  // Act
  bool exists = false;
  co::Run(*loop_, [&]() -> Co<> {
    auto result = co_await reader_->Exists(path);
    EXPECT_TRUE(result.has_value());
    exists = result.value();
  });

  // Assert
  EXPECT_TRUE(exists);
}

//! Verify Exists returns false for non-existent file.
NOLINT_TEST_F(WindowsFileReaderTest, Exists_NonExistent_ReturnsFalse)
{
  // Arrange
  auto path = test_dir_ / "nonexistent.txt";

  // Act
  bool exists = true;
  co::Run(*loop_, [&]() -> Co<> {
    auto result = co_await reader_->Exists(path);
    EXPECT_TRUE(result.has_value());
    exists = result.value();
  });

  // Assert
  EXPECT_FALSE(exists);
}

//! Verify Exists returns true for directory.
NOLINT_TEST_F(WindowsFileReaderTest, Exists_Directory_ReturnsTrue)
{
  // Arrange - use test_dir_

  // Act
  bool exists = false;
  co::Run(*loop_, [&]() -> Co<> {
    auto result = co_await reader_->Exists(test_dir_);
    EXPECT_TRUE(result.has_value());
    exists = result.value();
  });

  // Assert
  EXPECT_TRUE(exists);
}

//=== CreateAsyncFileReader Tests ===----------------------------------------//

//! Verify factory function creates reader.
NOLINT_TEST_F(WindowsFileReaderTest, CreateAsyncFileReader_ReturnsReader)
{
  // Arrange & Act
  auto reader = CreateAsyncFileReader(*loop_);

  // Assert
  EXPECT_NE(reader, nullptr);
}

} // namespace
