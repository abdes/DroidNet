//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/WindowsFileWriter.cpp

#include <Windows.h> // IWYU pragma: keep

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <errhandlingapi.h>
#include <fileapi.h>
#include <handleapi.h>
#include <winerror.h>
#include <winnt.h>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/FileError.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/WindowsFileReader.h>
#include <Oxygen/Cooker/Import/Internal/WindowsFileWriter.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::content::import;
using oxygen::cooker::test::ReadText;
using namespace oxygen::co;
namespace co = oxygen::co;

namespace {

//! Test fixture with temporary directory management.
class WindowsFileWriterTest : public oxygen::cooker::test::TempDirTest {
protected:
  auto SetUp() -> void override
  {
    loop_ = std::make_unique<ImportEventLoop>();
    writer_ = std::make_unique<WindowsFileWriter>(*loop_);
  }

  auto TearDown() -> void override
  {
    writer_.reset();
    loop_.reset();
  }

  //! Convert string to byte span for writing.
  static auto ToBytes(std::string_view str) -> std::span<const std::byte>
  {
    return std::as_bytes(std::span(str.data(), str.size()));
  }

  std::unique_ptr<ImportEventLoop> loop_;
  std::unique_ptr<WindowsFileWriter> writer_;
};

//=== Write Tests ===---------------------------------------------------------//

NOLINT_TEST_F(
  WindowsFileWriterTest, LongUnicodePathsSupportCompleteFileLifecycle)
{
  constexpr std::size_t kDirectoryLength = 100U;
  constexpr std::size_t kLegacyWindowsPathLimit = 260U;
  const auto path = TempDir() / std::string(kDirectoryLength, 'a')
    / std::string(kDirectoryLength, 'b')
    / std::filesystem::path(u8"模型-é.bin");
  ASSERT_GT(path.native().size(), kLegacyWindowsPathLimit);
  const std::string original = "native async bytes";
  WindowsFileReader reader(*loop_);
  co::Run(*loop_, [&] -> Co<> {
    const auto written = co_await writer_->Write(path, ToBytes(original));
    if (!written) {
      ADD_FAILURE() << written.error().ToString();
      co_return;
    }
    EXPECT_EQ(written.value(), original.size());
    const auto present = co_await reader.Exists(path);
    EXPECT_TRUE(present && present.value());
    const auto info = co_await reader.GetFileInfo(path);
    if (!info) {
      ADD_FAILURE() << info.error().ToString();
      co_return;
    }
    EXPECT_EQ(info.value().size, original.size());
    const auto bytes = co_await reader.ReadFile(path);
    if (!bytes) {
      ADD_FAILURE() << bytes.error().ToString();
      co_return;
    }
    EXPECT_EQ(bytes.value(),
      std::vector<std::byte>(
        ToBytes(original).begin(), ToBytes(original).end()));
  });
  EXPECT_EQ(oxygen::base::ComputeFileSha256(path),
    oxygen::base::ComputeSha256(ToBytes(original)));

  const std::string replacement = "atomically replaced bytes";
  ASSERT_TRUE(oxygen::serio::WriteFileAtomically(path, ToBytes(replacement)));
  EXPECT_EQ(oxygen::base::ComputeFileSha256(path),
    oxygen::base::ComputeSha256(ToBytes(replacement)));
  {
    oxygen::serio::FileStream<> stream(path, std::ios::in);
    const auto size = stream.Size();
    ASSERT_TRUE(size.has_value());
    EXPECT_EQ(size.value(), replacement.size());
  }
  const auto lock_path = path.parent_path() / "generation.lock";
  {
    const auto lock = oxygen::serio::FileLock::TryAcquire(lock_path,
      oxygen::serio::FileLockMode::kExclusive,
      oxygen::serio::FileLockOpenMode::kOpenOrCreate);
    ASSERT_TRUE(lock.has_value());
    const auto competing = oxygen::serio::FileLock::TryAcquire(
      lock_path, oxygen::serio::FileLockMode::kShared);
    ASSERT_FALSE(competing.has_value());
    EXPECT_EQ(competing.error(), std::errc::device_or_resource_busy);
  }
  EXPECT_TRUE(oxygen::serio::FileLock::TryAcquire(
    lock_path, oxygen::serio::FileLockMode::kShared));
}

NOLINT_TEST_F(WindowsFileWriterTest, WriteSmallFileWritesContent)
{
  const std::string content = "Hello, World!";
  auto path = TempDir() / "small.txt";

  std::optional<uint64_t> write_outcome;
  co::Run(*loop_, [&] -> Co<> {
    auto result = co_await writer_->Write(path, ToBytes(content));
    if (result.has_value()) {
      write_outcome = result.value();
    }
  });
  ASSERT_TRUE(write_outcome.has_value());
  const auto bytes_written = *write_outcome;

  EXPECT_EQ(bytes_written, content.size());
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_EQ(ReadText(path), content);
}

NOLINT_TEST_F(WindowsFileWriterTest, WriteLargerFileWritesContent)
{
  std::string content(64 * 1024, 'X'); // 64KB
  for (size_t i = 0; i < content.size(); ++i) {
    content.at(i) = static_cast<char>('A' + (i % 26));
  }
  auto path = TempDir() / "larger.bin";

  std::optional<uint64_t> write_outcome;
  co::Run(*loop_, [&] -> Co<> {
    auto result = co_await writer_->Write(path, ToBytes(content));
    if (result.has_value()) {
      write_outcome = result.value();
    }
  });
  ASSERT_TRUE(write_outcome.has_value());
  const auto bytes_written = *write_outcome;

  EXPECT_EQ(bytes_written, content.size());
  EXPECT_EQ(ReadText(path), content);
}

NOLINT_TEST_F(WindowsFileWriterTest, WriteEmptyDataCreatesEmptyFile)
{
  auto path = TempDir() / "empty.txt";

  std::optional<uint64_t> write_outcome;
  co::Run(*loop_, [&] -> Co<> {
    auto result = co_await writer_->Write(path, std::span<const std::byte> {});
    if (result.has_value()) {
      write_outcome = result.value();
    }
  });
  ASSERT_TRUE(write_outcome.has_value());
  const auto bytes_written = *write_outcome;

  EXPECT_EQ(bytes_written, 0U);
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_EQ(std::filesystem::file_size(path), 0U);
}

NOLINT_TEST_F(WindowsFileWriterTest, WriteOverwriteExistingReplacesContent)
{
  auto path = TempDir() / "overwrite.txt";
  const std::string original = "Original content that is quite long";
  const std::string replacement = "New content";

  // Create original file
  {
    std::ofstream file(path, std::ios::binary);
    file << original;
  }

  co::Run(*loop_, [&] -> Co<> {
    auto result = co_await writer_->Write(path, ToBytes(replacement));
    EXPECT_TRUE(result.has_value());
  });

  EXPECT_EQ(ReadText(path), replacement);
}

NOLINT_TEST_F(WindowsFileWriterTest, WriteNoOverwriteFailsIfExists)
{
  auto path = TempDir() / "existing.txt";
  {
    std::ofstream file(path);
    file << "existing";
  }

  FileError error = FileError::kOk;
  co::Run(*loop_, [&] -> Co<> {
    WriteOptions options;
    options.overwrite = false;
    auto result = co_await writer_->Write(path, ToBytes("new"), options);
    EXPECT_TRUE(result.has_error());
    error = result.error().code;
  });

  EXPECT_EQ(error, FileError::kAlreadyExists);
}

NOLINT_TEST_F(WindowsFileWriterTest, WriteCreateDirectoriesCreatesParents)
{
  auto path = TempDir() / "deep" / "nested" / "path" / "file.txt";
  const std::string content = "nested content";

  co::Run(*loop_, [&] -> Co<> {
    auto result = co_await writer_->Write(path, ToBytes(content));
    EXPECT_TRUE(result.has_value());
  });

  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_EQ(ReadText(path), content);
}

NOLINT_TEST_F(WindowsFileWriterTest, WriteNoCreateDirectoriesFailsIfMissing)
{
  auto path = TempDir() / "missing_parent" / "file.txt";

  FileError error = FileError::kOk;
  co::Run(*loop_, [&] -> Co<> {
    WriteOptions options;
    options.create_directories = false;
    auto result = co_await writer_->Write(path, ToBytes("content"), options);
    EXPECT_TRUE(result.has_error());
    error = result.error().code;
  });

  EXPECT_EQ(error, FileError::kNotFound);
}

NOLINT_TEST_F(WindowsFileWriterTest, WriteEmptyPathReturnsError)
{
  FileError error = FileError::kOk;
  co::Run(*loop_, [&] -> Co<> {
    auto result = co_await writer_->Write("", ToBytes("content"));
    EXPECT_TRUE(result.has_error());
    error = result.error().code;
  });

  EXPECT_EQ(error, FileError::kInvalidPath);
}

//=== WriteAt Tests ===-------------------------------------------------------//

NOLINT_TEST_F(WindowsFileWriterTest, WriteAtNewFileCreatesFile)
{
  auto path = TempDir() / "writeat_new.txt";
  const std::string content = "Initial content";

  std::optional<uint64_t> write_outcome;
  co::Run(*loop_, [&] -> Co<> {
    auto result = co_await writer_->WriteAt(path, 0, ToBytes(content));
    if (result.has_value()) {
      write_outcome = result.value();
    }
  });
  ASSERT_TRUE(write_outcome.has_value());
  const auto bytes_written = *write_outcome;

  EXPECT_EQ(bytes_written, content.size());
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_EQ(ReadText(path), content);
}

NOLINT_TEST_F(WindowsFileWriterTest, WriteAtExistingFilePreservesPrefix)
{
  auto path = TempDir() / "writeat_existing.txt";
  const std::string original = "Hello, World!";
  const std::string patch = "XYZ";

  {
    std::ofstream file(path, std::ios::binary);
    file << original;
  }

  co::Run(*loop_, [&] -> Co<> {
    // Overwrite starting at offset 7 ("World" begins at 7)
    auto result = co_await writer_->WriteAt(
      path, 7, ToBytes(patch), WriteOptions { .overwrite = false });
    EXPECT_TRUE(result.has_value());
  });

  EXPECT_EQ(ReadText(path), "Hello, XYZld!");
}

NOLINT_TEST_F(WindowsFileWriterTest, WriteAtCreateDirectoriesCreatesParents)
{
  auto path = TempDir() / "deep" / "writeat" / "path" / "file.bin";
  const std::string content = "nested content";

  co::Run(*loop_, [&] -> Co<> {
    auto result = co_await writer_->WriteAt(path, 0, ToBytes(content));
    EXPECT_TRUE(result.has_value());
  });

  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_EQ(ReadText(path), content);
}

//=== WriteAsync Tests ===----------------------------------------------------//

NOLINT_TEST_F(WindowsFileWriterTest, WriteAsyncCompletesWithCallback)
{
  auto path = TempDir() / "async_write.txt";
  const std::string content = "Async content";
  bool callback_invoked = false;
  uint64_t callback_bytes = 0;
  FileError callback_error = FileError::kUnknown;

  co::Run(*loop_, [&] -> Co<> {
    writer_->WriteAsync(path, ToBytes(content), {},
      [&](const FileErrorInfo& err, uint64_t bytes) -> void {
        callback_invoked = true;
        callback_error = err.code;
        callback_bytes = bytes;
      });

    // Wait for completion
    auto result = co_await writer_->Flush();
    EXPECT_TRUE(result.has_value());
  });

  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(callback_error, FileError::kOk);
  EXPECT_EQ(callback_bytes, content.size());
  EXPECT_EQ(ReadText(path), content);
}

NOLINT_TEST_F(WindowsFileWriterTest, WriteAsyncPendingCountTracked)
{
  auto path = TempDir() / "pending_test.txt";
  const std::string content = "content";

  // Assert initial state
  EXPECT_EQ(writer_->PendingCount(), 0U);
  EXPECT_FALSE(writer_->HasPending());

  // Start write without waiting
  writer_->WriteAsync(path, ToBytes(content), {}, nullptr);

  // The write is posted but may or may not have completed yet
  // Just verify Flush works
  co::Run(*loop_, [&] -> Co<> { co_await writer_->Flush(); });

  // After flush, pending should be 0
  EXPECT_EQ(writer_->PendingCount(), 0U);
}

//=== WriteAtAsync Tests ===--------------------------------------------------//

NOLINT_TEST_F(WindowsFileWriterTest, WriteAtAsyncCompletesWithCallback)
{
  auto path = TempDir() / "async_writeat.txt";
  const std::string content = "Async content";
  bool callback_invoked = false;
  uint64_t callback_bytes = 0;
  FileError callback_error = FileError::kUnknown;

  co::Run(*loop_, [&] -> Co<> {
    writer_->WriteAtAsync(path, 0, ToBytes(content), {},
      [&](const FileErrorInfo& err, uint64_t bytes) -> void {
        callback_invoked = true;
        callback_error = err.code;
        callback_bytes = bytes;
      });

    auto result = co_await writer_->Flush();
    EXPECT_TRUE(result.has_value());
  });

  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(callback_error, FileError::kOk);
  EXPECT_EQ(callback_bytes, content.size());
  EXPECT_EQ(ReadText(path), content);
}

NOLINT_TEST_F(
  WindowsFileWriterTest, WriteAtAsyncConcurrentNonOverlappingSucceeds)
{
  auto path = TempDir() / "async_writeat_concurrent.bin";
  const std::string a = "AAAA";
  const std::string b = "BBBB";
  std::atomic<int> completed { 0 };

  WriteOptions opts;
  opts.share_write = true;

  co::Run(*loop_, [&] -> Co<> {
    writer_->WriteAtAsync(path, 0, ToBytes(a), opts,
      [&](const FileErrorInfo& err, uint64_t bytes) -> void {
        EXPECT_EQ(err.code, FileError::kOk);
        EXPECT_EQ(bytes, a.size());
        completed.fetch_add(1, std::memory_order_relaxed);
      });

    writer_->WriteAtAsync(path, 8, ToBytes(b), opts,
      [&](const FileErrorInfo& err, uint64_t bytes) -> void {
        EXPECT_EQ(err.code, FileError::kOk);
        EXPECT_EQ(bytes, b.size());
        completed.fetch_add(1, std::memory_order_relaxed);
      });

    auto result = co_await writer_->Flush();
    EXPECT_TRUE(result.has_value());
  });

  EXPECT_EQ(completed.load(std::memory_order_relaxed), 2);
  const auto content = ReadText(path);
  ASSERT_GE(content.size(), 12U);
  EXPECT_EQ(content.substr(0, 4), a);
  EXPECT_EQ(content.substr(8, 4), b);
}

//=== Flush Tests ===---------------------------------------------------------//

//! A reader that allows writes must not cause a reciprocal sharing violation.
NOLINT_TEST_F(WindowsFileWriterTest, WriteAtAsyncAllowsCooperativeReaders)
{
  for (const auto share_write : { false, true }) {
    SCOPED_TRACE(share_write);
    const auto path = TempDir()
      / (share_write ? "shared_reader.bin" : "exclusive_reader.bin");
    const auto original = std::string("original");
    const auto appended = std::string(" appended");
    {
      auto file = std::ofstream(path, std::ios::binary);
      file << original;
    }
    auto* const reader = CreateFileW(path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(reader, INVALID_HANDLE_VALUE) << GetLastError();
    [[maybe_unused]] const auto close_reader = oxygen::Finally(
      [reader] -> void { static_cast<void>(CloseHandle(reader)); });
    auto callback_count = 0U;
    auto callback_error = FileErrorInfo {};
    auto callback_bytes = uint64_t { 0 };
    // Run owns this full expression until the coroutine and callbacks drain.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    co::Run(*loop_, [&] -> Co<> {
      writer_->WriteAtAsync(path, original.size(), ToBytes(appended),
        WriteOptions { .share_write = share_write },
        [&](const FileErrorInfo& error, const uint64_t count) -> void {
          ++callback_count;
          callback_error = error;
          callback_bytes = count;
        });
      const auto result = co_await writer_->Flush();
      EXPECT_TRUE(result.has_value());
    });
    EXPECT_EQ(callback_count, 1U);
    EXPECT_EQ(callback_error.code, FileError::kOk);
    EXPECT_EQ(callback_bytes, appended.size());
    EXPECT_EQ(writer_->PendingCount(), 0U);
    EXPECT_EQ(ReadText(path), original + appended);
  }
}

//! A reader's explicit write exclusion remains authoritative for both modes.
NOLINT_TEST_F(WindowsFileWriterTest, WriteAtAsyncRejectsReadersDenyingWrites)
{
  for (const auto share_write : { false, true }) {
    SCOPED_TRACE(share_write);
    const auto path = TempDir()
      / (share_write ? "protected_shared.bin" : "protected_exclusive.bin");
    const auto original = std::string("original");
    {
      auto file = std::ofstream(path, std::ios::binary);
      file << original;
    }
    auto* const reader = CreateFileW(path.c_str(), GENERIC_READ,
      FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(reader, INVALID_HANDLE_VALUE) << GetLastError();
    [[maybe_unused]] const auto close_reader = oxygen::Finally(
      [reader] -> void { static_cast<void>(CloseHandle(reader)); });
    auto callback_count = 0U;
    auto callback_error = FileErrorInfo {};
    auto callback_bytes = uint64_t { 1 };
    // Run owns this full expression until the coroutine and callbacks drain.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    co::Run(*loop_, [&] -> Co<> {
      writer_->WriteAtAsync(path, original.size(), ToBytes("rejected"),
        WriteOptions { .share_write = share_write },
        [&](const FileErrorInfo& error, const uint64_t count) -> void {
          ++callback_count;
          callback_error = error;
          callback_bytes = count;
        });
      const auto result = co_await writer_->Flush();
      EXPECT_TRUE(result.has_error());
      if (result.has_error()) {
        EXPECT_EQ(result.error().system_error.value(), ERROR_SHARING_VIOLATION);
      }
    });
    EXPECT_EQ(callback_count, 1U);
    EXPECT_NE(callback_error.code, FileError::kOk);
    EXPECT_EQ(callback_error.system_error.value(), ERROR_SHARING_VIOLATION);
    EXPECT_EQ(callback_bytes, 0U);
    EXPECT_EQ(writer_->PendingCount(), 0U);
    EXPECT_EQ(ReadText(path), original);
  }
}

//! Read sharing does not grant a second writer access when share_write is off.
NOLINT_TEST_F(WindowsFileWriterTest, WriteAtPreservesExclusiveWriteAccess)
{
  const auto path = TempDir() / "exclusive_writer.bin";
  auto* const held_writer = CreateFileW(path.c_str(), GENERIC_WRITE,
    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
    FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_NE(held_writer, INVALID_HANDLE_VALUE) << GetLastError();
  [[maybe_unused]] const auto close_writer = oxygen::Finally(
    [held_writer] -> void { static_cast<void>(CloseHandle(held_writer)); });
  // Run owns this full expression until the coroutine completes.
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&] -> Co<> {
    const auto result = co_await writer_->WriteAt(path, 0, ToBytes("rejected"));
    EXPECT_TRUE(result.has_error());
    if (result.has_error()) {
      EXPECT_EQ(result.error().system_error.value(), ERROR_SHARING_VIOLATION);
    }
  });
  EXPECT_EQ(std::filesystem::file_size(path), 0U);
}

NOLINT_TEST_F(WindowsFileWriterTest, FlushWaitsForAllPending)
{
  auto path1 = TempDir() / "flush1.txt";
  auto path2 = TempDir() / "flush2.txt";
  auto path3 = TempDir() / "flush3.txt";
  const std::string content = "content";
  int completed_count = 0;

  writer_->WriteAsync(path1, ToBytes(content), {},
    [&](auto, auto) -> auto { ++completed_count; });
  writer_->WriteAsync(path2, ToBytes(content), {},
    [&](auto, auto) -> auto { ++completed_count; });
  writer_->WriteAsync(path3, ToBytes(content), {},
    [&](auto, auto) -> auto { ++completed_count; });

  co::Run(*loop_, [&] -> Co<> {
    auto result = co_await writer_->Flush();
    EXPECT_TRUE(result.has_value());
  });

  EXPECT_EQ(completed_count, 3);
  EXPECT_TRUE(std::filesystem::exists(path1));
  EXPECT_TRUE(std::filesystem::exists(path2));
  EXPECT_TRUE(std::filesystem::exists(path3));
}

NOLINT_TEST_F(WindowsFileWriterTest, FlushReturnsFirstError)
{
  auto valid_path = TempDir() / "valid.txt";
  auto invalid_path = TempDir() / "missing_parent" / "file.txt";
  const std::string content = "content";

  WriteOptions no_create;
  no_create.create_directories = false;

  writer_->WriteAsync(valid_path, ToBytes(content), {}, nullptr);
  writer_->WriteAsync(invalid_path, ToBytes(content), no_create, nullptr);

  FileError error = FileError::kOk;
  co::Run(*loop_, [&] -> Co<> {
    auto result = co_await writer_->Flush();
    if (result.has_error()) {
      error = result.error().code;
    }
  });

  EXPECT_EQ(error, FileError::kNotFound);
}

//=== CancelAll Tests ===-----------------------------------------------------//

NOLINT_TEST_F(WindowsFileWriterTest, CancelAllPreventsNewOperations)
{
  auto path = TempDir() / "canceled.txt";

  writer_->CancelAll();

  FileError error = FileError::kOk;
  co::Run(*loop_, [&] -> Co<> {
    auto result = co_await writer_->Write(path, ToBytes("content"));
    EXPECT_TRUE(result.has_error());
    error = result.error().code;
  });

  EXPECT_EQ(error, FileError::kCancelled);
}

NOLINT_TEST_F(WindowsFileWriterTest, CancelAllInvokesCallbacksWithCancelled)
{
  auto path = TempDir() / "cancel_callback.txt";
  FileError callback_error = FileError::kOk;
  bool callback_invoked = false;

  // Cancel before starting
  writer_->CancelAll();

  writer_->WriteAsync(path, ToBytes("content"), {},
    [&](const FileErrorInfo& err, uint64_t) -> void {
      callback_invoked = true;
      callback_error = err.code;
    });

  // Callback should be invoked immediately with canceled
  EXPECT_TRUE(callback_invoked);
  EXPECT_EQ(callback_error, FileError::kCancelled);
}

//=== CreateAsyncFileWriter Tests ===----------------------------------------//

NOLINT_TEST_F(WindowsFileWriterTest, CreateAsyncFileWriterReturnsWriter)
{
  auto writer = CreateAsyncFileWriter(*loop_);

  EXPECT_NE(writer, nullptr);
}

} // namespace
