//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/WindowsFileReader.cpp

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/FileError.h>
#include <Oxygen/Cooker/Import/FileInfo.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/WindowsFileReader.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::content::import;
using namespace oxygen::co;
namespace co = oxygen::co;

namespace {

//! Test fixture with temporary file creation.
class WindowsFileReaderTest : public oxygen::cooker::test::TempDirTest {
protected:
  auto SetUp() -> void override
  {
    loop_ = std::make_unique<ImportEventLoop>();
    reader_ = std::make_unique<WindowsFileReader>(*loop_);
  }

  auto TearDown() -> void override
  {
    reader_.reset();
    loop_.reset();
  }

  //! Create a test file with specified content.
  auto CreateTestFile(std::string_view name, std::span<const std::byte> content)
    -> std::filesystem::path
  {
    auto path = TempDir() / name;
    oxygen::cooker::test::WriteBytes(path, content);
    return path;
  }

  //! Create a test file with string content.
  auto CreateTestFile(std::string_view name, std::string_view content)
    -> std::filesystem::path
  {
    auto path = TempDir() / name;
    oxygen::cooker::test::WriteText(path, content);
    return path;
  }

  std::unique_ptr<ImportEventLoop> loop_;
  std::unique_ptr<WindowsFileReader> reader_;
};

//=== ReadFile Tests ===------------------------------------------------------//

NOLINT_TEST_F(WindowsFileReaderTest, ReadFileSmallFileReadsAllContent)
{
  const std::string content = "Hello, World!";
  auto path = CreateTestFile("small.txt", content);

  std::optional<std::vector<std::byte>> result_outcome;
  co::Run(*loop_, [&] -> Co<> {
    auto read_result = co_await reader_->ReadFile(path);
    if (read_result.has_value()) {
      result_outcome = std::move(read_result).value();
    }
  });
  ASSERT_HAS_VALUE(result_outcome);
  const auto& result = *result_outcome;

  EXPECT_EQ(result.size(), content.size());
  std::string result_str(
    reinterpret_cast<const char*>(result.data()), result.size());
  EXPECT_EQ(result_str, content);
}

NOLINT_TEST_F(WindowsFileReaderTest, ReadFileLargerFileReadsAllContent)
{
  std::string content(64 * 1024, 'X'); // 64KB
  for (size_t i = 0; i < content.size(); ++i) {
    content.at(i) = static_cast<char>('A' + (i % 26));
  }
  auto path = CreateTestFile("larger.bin", content);

  std::optional<std::vector<std::byte>> result_outcome;
  co::Run(*loop_, [&] -> Co<> {
    auto read_result = co_await reader_->ReadFile(path);
    if (read_result.has_value()) {
      result_outcome = std::move(read_result).value();
    }
  });
  ASSERT_HAS_VALUE(result_outcome);
  const auto& result = *result_outcome;

  EXPECT_EQ(result.size(), content.size());
  std::string result_str(
    reinterpret_cast<const char*>(result.data()), result.size());
  EXPECT_EQ(result_str, content);
}

NOLINT_TEST_F(WindowsFileReaderTest, ReadFileWithOffsetReadsFromOffset)
{
  const std::string content = "Hello, World!";
  auto path = CreateTestFile("offset.txt", content);

  std::optional<std::vector<std::byte>> result_outcome;
  co::Run(*loop_, [&] -> Co<> {
    ReadOptions options;
    options.offset = 7; // Skip "Hello, "
    auto read_result = co_await reader_->ReadFile(path, options);
    if (read_result.has_value()) {
      result_outcome = std::move(read_result).value();
    }
  });
  ASSERT_HAS_VALUE(result_outcome);
  const auto& result = *result_outcome;

  EXPECT_EQ(result.size(), 6U); // "World!"
  std::string result_str(
    reinterpret_cast<const char*>(result.data()), result.size());
  EXPECT_EQ(result_str, "World!");
}

NOLINT_TEST_F(WindowsFileReaderTest, ReadFileWithMaxBytesLimitsRead)
{
  const std::string content = "Hello, World!";
  auto path = CreateTestFile("limited.txt", content);

  std::optional<std::vector<std::byte>> result_outcome;
  co::Run(*loop_, [&] -> Co<> {
    ReadOptions options;
    options.max_bytes = 5; // Only read "Hello"
    auto read_result = co_await reader_->ReadFile(path, options);
    if (read_result.has_value()) {
      result_outcome = std::move(read_result).value();
    }
  });
  ASSERT_HAS_VALUE(result_outcome);
  const auto& result = *result_outcome;

  EXPECT_EQ(result.size(), 5U);
  std::string result_str(
    reinterpret_cast<const char*>(result.data()), result.size());
  EXPECT_EQ(result_str, "Hello");
}

NOLINT_TEST_F(WindowsFileReaderTest, ReadFileWithOffsetAndMaxBytesWorks)
{
  const std::string content = "Hello, World!";
  auto path = CreateTestFile("combo.txt", content);

  std::optional<std::vector<std::byte>> result_outcome;
  co::Run(*loop_, [&] -> Co<> {
    ReadOptions options;
    options.offset = 7;
    options.max_bytes = 5; // "World" without "!"
    auto read_result = co_await reader_->ReadFile(path, options);
    if (read_result.has_value()) {
      result_outcome = std::move(read_result).value();
    }
  });
  ASSERT_HAS_VALUE(result_outcome);
  const auto& result = *result_outcome;

  EXPECT_EQ(result.size(), 5U);
  std::string result_str(
    reinterpret_cast<const char*>(result.data()), result.size());
  EXPECT_EQ(result_str, "World");
}

NOLINT_TEST_F(WindowsFileReaderTest, ReadFileNonExistentReturnsError)
{
  auto path = TempDir() / "nonexistent.txt";

  FileError error = FileError::kOk;
  co::Run(*loop_, [&] -> Co<> {
    auto read_result = co_await reader_->ReadFile(path);
    EXPECT_TRUE(read_result.has_error());
    error = read_result.error().code;
  });

  EXPECT_EQ(error, FileError::kNotFound);
}

NOLINT_TEST_F(WindowsFileReaderTest, ReadFileOffsetPastEOFReturnsEmpty)
{
  const std::string content = "Hello";
  auto path = CreateTestFile("short.txt", content);

  std::optional<std::vector<std::byte>> result_outcome;
  co::Run(*loop_, [&] -> Co<> {
    ReadOptions options;
    options.offset = 100; // Past EOF
    auto read_result = co_await reader_->ReadFile(path, options);
    if (read_result.has_value()) {
      result_outcome = std::move(read_result).value();
    }
  });
  ASSERT_HAS_VALUE(result_outcome);
  const auto& result = *result_outcome;

  EXPECT_TRUE(result.empty());
}

//=== GetFileInfo Tests ===---------------------------------------------------//

NOLINT_TEST_F(WindowsFileReaderTest, GetFileInfoExistingFileReturnsInfo)
{
  const std::string content = "Test content";
  auto path = CreateTestFile("info.txt", content);

  std::optional<FileInfo> info_outcome;
  co::Run(*loop_, [&] -> Co<> {
    auto result = co_await reader_->GetFileInfo(path);
    if (result.has_value()) {
      info_outcome = result.value();
    }
  });
  ASSERT_HAS_VALUE(info_outcome);
  const auto& info = *info_outcome;

  EXPECT_EQ(info.size, content.size());
  EXPECT_FALSE(info.is_directory);
  EXPECT_FALSE(info.is_symlink);
}

NOLINT_TEST_F(WindowsFileReaderTest, GetFileInfoDirectoryReturnsInfo)
{
  // Use TempDir() which already exists

  std::optional<FileInfo> info_outcome;
  co::Run(*loop_, [&] -> Co<> {
    auto result = co_await reader_->GetFileInfo(TempDir());
    if (result.has_value()) {
      info_outcome = result.value();
    }
  });
  ASSERT_HAS_VALUE(info_outcome);
  const auto& info = *info_outcome;

  EXPECT_TRUE(info.is_directory);
}

NOLINT_TEST_F(WindowsFileReaderTest, GetFileInfoNonExistentReturnsError)
{
  auto path = TempDir() / "nonexistent.txt";

  FileError error = FileError::kOk;
  co::Run(*loop_, [&] -> Co<> {
    auto result = co_await reader_->GetFileInfo(path);
    EXPECT_TRUE(result.has_error());
    error = result.error().code;
  });

  EXPECT_EQ(error, FileError::kNotFound);
}

//=== Exists Tests ===--------------------------------------------------------//

NOLINT_TEST_F(WindowsFileReaderTest, ExistsExistingFileReturnsTrue)
{
  auto path = CreateTestFile("exists.txt", "content");

  std::optional<bool> exists_outcome;
  co::Run(*loop_, [&] -> Co<> {
    auto result = co_await reader_->Exists(path);
    if (result.has_value()) {
      exists_outcome = result.value();
    }
  });
  ASSERT_HAS_VALUE(exists_outcome);
  const auto& exists = *exists_outcome;

  EXPECT_TRUE(exists);
}

NOLINT_TEST_F(WindowsFileReaderTest, ExistsNonExistentReturnsFalse)
{
  auto path = TempDir() / "nonexistent.txt";

  std::optional<bool> exists_outcome;
  co::Run(*loop_, [&] -> Co<> {
    auto result = co_await reader_->Exists(path);
    if (result.has_value()) {
      exists_outcome = result.value();
    }
  });
  ASSERT_HAS_VALUE(exists_outcome);
  const auto& exists = *exists_outcome;

  EXPECT_FALSE(exists);
}

NOLINT_TEST_F(WindowsFileReaderTest, ExistsDirectoryReturnsTrue)
{
  // Use TempDir()

  std::optional<bool> exists_outcome;
  co::Run(*loop_, [&] -> Co<> {
    auto result = co_await reader_->Exists(TempDir());
    if (result.has_value()) {
      exists_outcome = result.value();
    }
  });
  ASSERT_HAS_VALUE(exists_outcome);
  const auto& exists = *exists_outcome;

  EXPECT_TRUE(exists);
}

//=== CreateAsyncFileReader Tests ===----------------------------------------//

NOLINT_TEST_F(WindowsFileReaderTest, CreateAsyncFileReaderReturnsReader)
{
  auto reader = CreateAsyncFileReader(*loop_);

  EXPECT_NE(reader, nullptr);
}

} // namespace
