//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/Async/WindowsFileWriter.h>

#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Testing/GTest.h>

#include <filesystem>
#include <fstream>
#include <latch>

using namespace oxygen::content::import;
using namespace oxygen::co;
namespace co = oxygen::co;

namespace {

//! Test fixture with temporary directory management.
class WindowsFileWriterTest : public ::testing::Test {
protected:
  auto SetUp() -> void override
  {
    loop_ = std::make_unique<ImportEventLoop>();
    writer_ = std::make_unique<WindowsFileWriter>(*loop_);
    test_dir_
      = std::filesystem::temp_directory_path() / "oxygen_file_writer_test";
    std::filesystem::create_directories(test_dir_);
  }

  auto TearDown() -> void override
  {
    writer_.reset();
    loop_.reset();
    std::error_code ec;
    std::filesystem::remove_all(test_dir_, ec);
  }

  //! Convert string to byte span for writing.
  static auto ToBytes(std::string_view str) -> std::span<const std::byte>
  {
    return std::as_bytes(std::span(str.data(), str.size()));
  }

  //! Read file content for verification.
  auto ReadFileContent(const std::filesystem::path& path) -> std::string
  {
    std::ifstream file(path, std::ios::binary);
    return std::string(
      std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  }

  std::unique_ptr<ImportEventLoop> loop_;
  std::unique_ptr<WindowsFileWriter> writer_;
  std::filesystem::path test_dir_;
};

//=== Write Tests ===---------------------------------------------------------//

//! Verify writing a small file.
NOLINT_TEST_F(WindowsFileWriterTest, Write_SmallFile_WritesContent)
{
  // Arrange
  const std::string content = "Hello, World!";
  auto path = test_dir_ / "small.txt";

  // Act
  uint64_t bytes_written = 0;
  co::Run(*loop_, [&]() -> Co<> {
    auto result = co_await writer_->Write(path, ToBytes(content));
    EXPECT_TRUE(result.has_value());
    bytes_written = result.value();
  });

  // Assert
  EXPECT_EQ(bytes_written, content.size());
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_EQ(ReadFileContent(path), content);
}

//! Verify writing a larger file.
NOLINT_TEST_F(WindowsFileWriterTest, Write_LargerFile_WritesContent)
{
  // Arrange
  std::string content(64 * 1024, 'X'); // 64KB
  for (size_t i = 0; i < content.size(); ++i) {
    content[i] = static_cast<char>('A' + (i % 26));
  }
  auto path = test_dir_ / "larger.bin";

  // Act
  uint64_t bytes_written = 0;
  co::Run(*loop_, [&]() -> Co<> {
    auto result = co_await writer_->Write(path, ToBytes(content));
    EXPECT_TRUE(result.has_value());
    bytes_written = result.value();
  });

  // Assert
  EXPECT_EQ(bytes_written, content.size());
  EXPECT_EQ(ReadFileContent(path), content);
}

//! Verify writing empty data creates empty file.
NOLINT_TEST_F(WindowsFileWriterTest, Write_EmptyData_CreatesEmptyFile)
{
  // Arrange
  auto path = test_dir_ / "empty.txt";

  // Act
  uint64_t bytes_written = 0;
  co::Run(*loop_, [&]() -> Co<> {
    auto result = co_await writer_->Write(path, std::span<const std::byte> {});
    EXPECT_TRUE(result.has_value());
    bytes_written = result.value();
  });

  // Assert
  EXPECT_EQ(bytes_written, 0u);
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_EQ(std::filesystem::file_size(path), 0u);
}

//! Verify overwrite mode replaces existing content.
NOLINT_TEST_F(WindowsFileWriterTest, Write_OverwriteExisting_ReplacesContent)
{
  // Arrange
  auto path = test_dir_ / "overwrite.txt";
  const std::string original = "Original content that is quite long";
  const std::string replacement = "New content";

  // Create original file
  {
    std::ofstream file(path, std::ios::binary);
    file << original;
  }

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    auto result = co_await writer_->Write(path, ToBytes(replacement));
    EXPECT_TRUE(result.has_value());
  });

  // Assert
  EXPECT_EQ(ReadFileContent(path), replacement);
}

//! Verify write fails when overwrite=false and file exists.
NOLINT_TEST_F(WindowsFileWriterTest, Write_NoOverwrite_FailsIfExists)
{
  // Arrange
  auto path = test_dir_ / "existing.txt";
  {
    std::ofstream file(path);
    file << "existing";
  }

  // Act
  FileError error = FileError::kOk;
  co::Run(*loop_, [&]() -> Co<> {
    WriteOptions options;
    options.overwrite = false;
    auto result = co_await writer_->Write(path, ToBytes("new"), options);
    EXPECT_TRUE(result.has_error());
    error = result.error().code;
  });

  // Assert
  EXPECT_EQ(error, FileError::kAlreadyExists);
}

//! Verify write creates parent directories.
NOLINT_TEST_F(WindowsFileWriterTest, Write_CreateDirectories_CreatesParents)
{
  // Arrange
  auto path = test_dir_ / "deep" / "nested" / "path" / "file.txt";
  const std::string content = "nested content";

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    auto result = co_await writer_->Write(path, ToBytes(content));
    EXPECT_TRUE(result.has_value());
  });

  // Assert
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_EQ(ReadFileContent(path), content);
}

//! Verify write fails if create_directories=false and parent missing.
NOLINT_TEST_F(WindowsFileWriterTest, Write_NoCreateDirectories_FailsIfMissing)
{
  // Arrange
  auto path = test_dir_ / "missing_parent" / "file.txt";

  // Act
  FileError error = FileError::kOk;
  co::Run(*loop_, [&]() -> Co<> {
    WriteOptions options;
    options.create_directories = false;
    auto result = co_await writer_->Write(path, ToBytes("content"), options);
    EXPECT_TRUE(result.has_error());
    error = result.error().code;
  });

  // Assert
  EXPECT_EQ(error, FileError::kNotFound);
}

//! Verify empty path returns error.
NOLINT_TEST_F(WindowsFileWriterTest, Write_EmptyPath_ReturnsError)
{
  // Arrange & Act
  FileError error = FileError::kOk;
  co::Run(*loop_, [&]() -> Co<> {
    auto result = co_await writer_->Write("", ToBytes("content"));
    EXPECT_TRUE(result.has_error());
    error = result.error().code;
  });

  // Assert
  EXPECT_EQ(error, FileError::kInvalidPath);
}

//=== WriteAt Tests ===-------------------------------------------------------//

//! Verify writing at offset 0 to a new file creates it.
NOLINT_TEST_F(WindowsFileWriterTest, WriteAt_NewFile_CreatesFile)
{
  // Arrange
  auto path = test_dir_ / "writeat_new.txt";
  const std::string content = "Initial content";

  // Act
  uint64_t bytes_written = 0;
  co::Run(*loop_, [&]() -> Co<> {
    auto result = co_await writer_->WriteAt(path, 0, ToBytes(content));
    EXPECT_TRUE(result.has_value());
    bytes_written = result.value();
  });

  // Assert
  EXPECT_EQ(bytes_written, content.size());
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_EQ(ReadFileContent(path), content);
}

//! Verify writing at a non-zero offset preserves existing content.
NOLINT_TEST_F(WindowsFileWriterTest, WriteAt_ExistingFile_PreservesPrefix)
{
  // Arrange
  auto path = test_dir_ / "writeat_existing.txt";
  const std::string original = "Hello, World!";
  const std::string patch = "XYZ";

  {
    std::ofstream file(path, std::ios::binary);
    file << original;
  }

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    // Overwrite starting at offset 7 ("World" begins at 7)
    auto result = co_await writer_->WriteAt(
      path, 7, ToBytes(patch), WriteOptions { .overwrite = false });
    EXPECT_TRUE(result.has_value());
  });

  // Assert
  EXPECT_EQ(ReadFileContent(path), "Hello, XYZld!");
}

//! Verify writing at offset creates parent directories.
NOLINT_TEST_F(WindowsFileWriterTest, WriteAt_CreateDirectories_CreatesParents)
{
  // Arrange
  auto path = test_dir_ / "deep" / "writeat" / "path" / "file.bin";
  const std::string content = "nested content";

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    auto result = co_await writer_->WriteAt(path, 0, ToBytes(content));
    EXPECT_TRUE(result.has_value());
  });

  // Assert
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_EQ(ReadFileContent(path), content);
}

//=== WriteAsync Tests ===----------------------------------------------------//

//! Verify async write completes and invokes callback.
NOLINT_TEST_F(WindowsFileWriterTest, WriteAsync_CompletesWithCallback)
{
  // Arrange
  auto path = test_dir_ / "async_write.txt";
  const std::string content = "Async content";
  bool callback_invoked = false;
  uint64_t callback_bytes = 0;
  FileError callback_error = FileError::kUnknown;

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    writer_->WriteAsync(path, ToBytes(content), {},
      [&](const FileErrorInfo& err, uint64_t bytes) {
        callback_invoked = true;
        callback_error = err.code;
        callback_bytes = bytes;
      });

    // Wait for completion
    auto result = co_await writer_->Flush();
    EXPECT_TRUE(result.has_value());
  });

  // Assert
  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(callback_error, FileError::kOk);
  EXPECT_EQ(callback_bytes, content.size());
  EXPECT_EQ(ReadFileContent(path), content);
}

//! Verify pending count tracks async operations.
NOLINT_TEST_F(WindowsFileWriterTest, WriteAsync_PendingCountTracked)
{
  // Arrange
  auto path = test_dir_ / "pending_test.txt";
  const std::string content = "content";

  // Assert initial state
  EXPECT_EQ(writer_->PendingCount(), 0u);
  EXPECT_FALSE(writer_->HasPending());

  // Act - start write without waiting
  writer_->WriteAsync(path, ToBytes(content), {}, nullptr);

  // The write is posted but may or may not have completed yet
  // Just verify Flush works
  co::Run(*loop_, [&]() -> Co<> { co_await writer_->Flush(); });

  // Assert - after flush, pending should be 0
  EXPECT_EQ(writer_->PendingCount(), 0u);
}

//=== WriteAtAsync Tests ===--------------------------------------------------//

//! Verify async offset write completes and invokes callback with bytes written.
NOLINT_TEST_F(WindowsFileWriterTest, WriteAtAsync_CompletesWithCallback)
{
  // Arrange
  auto path = test_dir_ / "async_writeat.txt";
  const std::string content = "Async content";
  bool callback_invoked = false;
  uint64_t callback_bytes = 0;
  FileError callback_error = FileError::kUnknown;

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    writer_->WriteAtAsync(path, 0, ToBytes(content), {},
      [&](const FileErrorInfo& err, uint64_t bytes) {
        callback_invoked = true;
        callback_error = err.code;
        callback_bytes = bytes;
      });

    auto result = co_await writer_->Flush();
    EXPECT_TRUE(result.has_value());
  });

  // Assert
  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(callback_error, FileError::kOk);
  EXPECT_EQ(callback_bytes, content.size());
  EXPECT_EQ(ReadFileContent(path), content);
}

//! Verify concurrent non-overlapping writes to the same file succeed with
//! share_write.
NOLINT_TEST_F(
  WindowsFileWriterTest, WriteAtAsync_ConcurrentNonOverlapping_Succeeds)
{
  // Arrange
  auto path = test_dir_ / "async_writeat_concurrent.bin";
  const std::string a = "AAAA";
  const std::string b = "BBBB";
  std::atomic<int> completed { 0 };

  WriteOptions opts;
  opts.share_write = true;

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    writer_->WriteAtAsync(
      path, 0, ToBytes(a), opts, [&](const FileErrorInfo& err, uint64_t bytes) {
        EXPECT_EQ(err.code, FileError::kOk);
        EXPECT_EQ(bytes, a.size());
        completed.fetch_add(1, std::memory_order_relaxed);
      });

    writer_->WriteAtAsync(
      path, 8, ToBytes(b), opts, [&](const FileErrorInfo& err, uint64_t bytes) {
        EXPECT_EQ(err.code, FileError::kOk);
        EXPECT_EQ(bytes, b.size());
        completed.fetch_add(1, std::memory_order_relaxed);
      });

    auto result = co_await writer_->Flush();
    EXPECT_TRUE(result.has_value());
  });

  // Assert
  EXPECT_EQ(completed.load(std::memory_order_relaxed), 2);
  const auto content = ReadFileContent(path);
  ASSERT_GE(content.size(), 12u);
  EXPECT_EQ(content.substr(0, 4), a);
  EXPECT_EQ(content.substr(8, 4), b);
}

//=== Flush Tests ===---------------------------------------------------------//

//! Verify Flush waits for all pending operations.
NOLINT_TEST_F(WindowsFileWriterTest, Flush_WaitsForAllPending)
{
  // Arrange
  auto path1 = test_dir_ / "flush1.txt";
  auto path2 = test_dir_ / "flush2.txt";
  auto path3 = test_dir_ / "flush3.txt";
  const std::string content = "content";
  int completed_count = 0;

  // Act
  writer_->WriteAsync(
    path1, ToBytes(content), {}, [&](auto, auto) { ++completed_count; });
  writer_->WriteAsync(
    path2, ToBytes(content), {}, [&](auto, auto) { ++completed_count; });
  writer_->WriteAsync(
    path3, ToBytes(content), {}, [&](auto, auto) { ++completed_count; });

  co::Run(*loop_, [&]() -> Co<> {
    auto result = co_await writer_->Flush();
    EXPECT_TRUE(result.has_value());
  });

  // Assert
  EXPECT_EQ(completed_count, 3);
  EXPECT_TRUE(std::filesystem::exists(path1));
  EXPECT_TRUE(std::filesystem::exists(path2));
  EXPECT_TRUE(std::filesystem::exists(path3));
}

//! Verify Flush returns first error if any operation failed.
NOLINT_TEST_F(WindowsFileWriterTest, Flush_ReturnsFirstError)
{
  // Arrange
  auto valid_path = test_dir_ / "valid.txt";
  auto invalid_path = test_dir_ / "missing_parent" / "file.txt";
  const std::string content = "content";

  WriteOptions no_create;
  no_create.create_directories = false;

  // Act
  writer_->WriteAsync(valid_path, ToBytes(content), {}, nullptr);
  writer_->WriteAsync(invalid_path, ToBytes(content), no_create, nullptr);

  FileError error = FileError::kOk;
  co::Run(*loop_, [&]() -> Co<> {
    auto result = co_await writer_->Flush();
    if (result.has_error()) {
      error = result.error().code;
    }
  });

  // Assert
  EXPECT_EQ(error, FileError::kNotFound);
}

//=== CancelAll Tests ===-----------------------------------------------------//

//! Verify CancelAll prevents new operations.
NOLINT_TEST_F(WindowsFileWriterTest, CancelAll_PreventsNewOperations)
{
  // Arrange
  auto path = test_dir_ / "cancelled.txt";

  // Act
  writer_->CancelAll();

  FileError error = FileError::kOk;
  co::Run(*loop_, [&]() -> Co<> {
    auto result = co_await writer_->Write(path, ToBytes("content"));
    EXPECT_TRUE(result.has_error());
    error = result.error().code;
  });

  // Assert
  EXPECT_EQ(error, FileError::kCancelled);
}

//! Verify CancelAll invokes callbacks with cancelled error.
NOLINT_TEST_F(WindowsFileWriterTest, CancelAll_InvokesCallbacksWithCancelled)
{
  // Arrange
  auto path = test_dir_ / "cancel_callback.txt";
  FileError callback_error = FileError::kOk;
  bool callback_invoked = false;

  // Cancel before starting
  writer_->CancelAll();

  // Act
  writer_->WriteAsync(
    path, ToBytes("content"), {}, [&](const FileErrorInfo& err, uint64_t) {
      callback_invoked = true;
      callback_error = err.code;
    });

  // Assert - callback should be invoked immediately with cancelled
  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(callback_error, FileError::kCancelled);
}

//=== CreateAsyncFileWriter Tests ===----------------------------------------//

//! Verify factory function creates writer.
NOLINT_TEST_F(WindowsFileWriterTest, CreateAsyncFileWriter_ReturnsWriter)
{
  // Arrange & Act
  auto writer = CreateAsyncFileWriter(*loop_);

  // Assert
  EXPECT_NE(writer, nullptr);
}

} // namespace
