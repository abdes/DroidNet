//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/FileSourceReader.cpp

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Import/FileInfo.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

using oxygen::content::import::IAsyncFileReader;
using oxygen::content::import::ImportEventLoop;
using oxygen::content::import::ReadOptions;
namespace co = oxygen::co;
namespace serio = oxygen::serio;

namespace {

//! Which driver the reader reads through.
enum class DriverKind : uint8_t {
  kLoop, //!< The import loop's driver: reads suspend and resume on the loop.
  kInline, //!< An inline driver: reads complete on the awaiting thread.
};

auto ToString(const serio::ByteBuffer& buffer) -> std::string
{
  const auto bytes = buffer.Bytes();
  auto text = std::string(bytes.size(), '\0');
  if (!bytes.empty()) {
    std::memcpy(text.data(), bytes.data(), bytes.size());
  }
  return text;
}

//! The reader over a real temporary folder, through both drivers.
class FileSourceReaderTest : public oxygen::testing::TempDirTest,
                             public ::testing::WithParamInterface<DriverKind> {
protected:
  auto SetUp() -> void override
  {
    loop_ = std::make_unique<ImportEventLoop>();
    if (GetParam() == DriverKind::kInline) {
      auto driver = serio::IoDriver::CreateInline();
      ASSERT_TRUE(driver);
      inline_driver_.emplace(std::move(driver).value());
    }
    reader_ = oxygen::content::import::CreateAsyncFileReader(
      inline_driver_ ? *inline_driver_ : loop_->Driver());
  }

  auto TearDown() -> void override
  {
    reader_.reset();
    inline_driver_.reset();
    loop_.reset();
  }

  //! Creates a file with `content` in the temporary folder.
  auto CreateFile(const std::filesystem::path& name, std::string_view content)
    -> std::filesystem::path
  {
    auto path = TempDir() / name;
    oxygen::cooker::test::WriteText(path, content);
    return path;
  }

  //! Reads `path` with `options` on the loop and returns the result.
  auto Read(const std::filesystem::path& path, ReadOptions options = {})
    -> oxygen::Result<serio::ByteBuffer>
  {
    return co::Run(*loop_, reader_->ReadFile(path, options));
  }

  std::unique_ptr<ImportEventLoop> loop_;
  std::optional<serio::IoDriver> inline_driver_;
  std::unique_ptr<IAsyncFileReader> reader_;
};

INSTANTIATE_TEST_SUITE_P(Drivers, FileSourceReaderTest,
  ::testing::Values(DriverKind::kLoop, DriverKind::kInline),
  [](const ::testing::TestParamInfo<DriverKind>& info) -> std::string {
    return info.param == DriverKind::kLoop ? "LoopDriver" : "InlineDriver";
  });

//=== ReadFile ===------------------------------------------------------------//

NOLINT_TEST_P(FileSourceReaderTest, ReadFileReadsTheWholeFile)
{
  const std::string content = "Hello, World!";
  const auto path = CreateFile("small.txt", content);

  const auto read = Read(path);

  ASSERT_TRUE(read);
  EXPECT_EQ(ToString(*read), content);
}

NOLINT_TEST_P(FileSourceReaderTest, ReadFileReadsALargerFile)
{
  std::string content(size_t { 64 } * 1024U, 'X');
  for (size_t i = 0; i < content.size(); ++i) {
    content.at(i) = static_cast<char>('A' + (i % 26));
  }
  const auto path = CreateFile("larger.bin", content);

  const auto read = Read(path);

  ASSERT_TRUE(read);
  EXPECT_EQ(ToString(*read), content);
}

NOLINT_TEST_P(FileSourceReaderTest, ReadFileOfAnEmptyFileIsEmpty)
{
  const auto path = CreateFile("empty.bin", "");

  const auto read = Read(path);

  ASSERT_TRUE(read);
  EXPECT_TRUE(read->Empty());
}

NOLINT_TEST_P(FileSourceReaderTest, ReadFileWithOffsetReadsFromOffset)
{
  const auto path = CreateFile("offset.txt", "Hello, World!");

  const auto read = Read(path, ReadOptions { .offset = 7 });

  ASSERT_TRUE(read);
  EXPECT_EQ(ToString(*read), "World!");
}

NOLINT_TEST_P(FileSourceReaderTest, ReadFileWithMaxBytesLimitsTheRead)
{
  const auto path = CreateFile("limited.txt", "Hello, World!");

  const auto read = Read(path, ReadOptions { .max_bytes = 5 });

  ASSERT_TRUE(read);
  EXPECT_EQ(ToString(*read), "Hello");
}

NOLINT_TEST_P(FileSourceReaderTest, ReadFileWithOffsetAndMaxBytesReadsTheRange)
{
  const auto path = CreateFile("range.txt", "Hello, World!");

  const auto read = Read(path, ReadOptions { .offset = 7, .max_bytes = 5 });

  ASSERT_TRUE(read);
  EXPECT_EQ(ToString(*read), "World");
}

NOLINT_TEST_P(FileSourceReaderTest, ReadFileRangePastTheEndIsClamped)
{
  const auto path = CreateFile("clamped.txt", "Hello");

  const auto read = Read(path, ReadOptions { .offset = 3, .max_bytes = 100 });

  ASSERT_TRUE(read);
  EXPECT_EQ(ToString(*read), "lo");
}

NOLINT_TEST_P(FileSourceReaderTest, ReadFileOffsetPastTheEndReadsNothing)
{
  const auto path = CreateFile("short.txt", "Hello");

  const auto read = Read(path, ReadOptions { .offset = 100 });

  ASSERT_TRUE(read);
  EXPECT_TRUE(read->Empty());
}

NOLINT_TEST_P(FileSourceReaderTest, ReadFileOfAMissingFileIsNotFound)
{
  const auto read = Read(TempDir() / "missing.txt");

  ASSERT_FALSE(read);
  EXPECT_EQ(read.error(), serio::Error::kNotFound);
}

NOLINT_TEST_P(FileSourceReaderTest, ReadFileOfADirectoryIsADirectoryError)
{
  const auto read = Read(TempDir());

  ASSERT_FALSE(read);
  EXPECT_EQ(read.error(), serio::Error::kIsDirectory);
}

NOLINT_TEST_P(FileSourceReaderTest, ReadFileOfAnEmptyPathIsInvalid)
{
  const auto read = Read(std::filesystem::path {});

  ASSERT_FALSE(read);
  EXPECT_EQ(read.error(), std::errc::invalid_argument);
}

//=== GetFileInfo ===---------------------------------------------------------//

NOLINT_TEST_P(FileSourceReaderTest, GetFileInfoOfAFileReportsItsSize)
{
  const std::string content = "Test content";
  const auto path = CreateFile("info.txt", content);

  const auto info = co::Run(*loop_, reader_->GetFileInfo(path));

  ASSERT_TRUE(info);
  EXPECT_EQ(info->size, content.size());
  EXPECT_FALSE(info->is_directory);
  EXPECT_FALSE(info->is_symlink);
}

NOLINT_TEST_P(FileSourceReaderTest, GetFileInfoOfADirectoryReportsIt)
{
  const auto info = co::Run(*loop_, reader_->GetFileInfo(TempDir()));

  ASSERT_TRUE(info);
  EXPECT_TRUE(info->is_directory);
}

NOLINT_TEST_P(FileSourceReaderTest, GetFileInfoOfAMissingFileIsNotFound)
{
  const auto info
    = co::Run(*loop_, reader_->GetFileInfo(TempDir() / "missing.txt"));

  ASSERT_FALSE(info);
  EXPECT_EQ(info.error(), serio::Error::kNotFound);
}

//=== Exists ===--------------------------------------------------------------//

NOLINT_TEST_P(FileSourceReaderTest, ExistsIsTrueForAFileAndAFolder)
{
  const auto path = CreateFile("exists.txt", "content");

  const auto file = co::Run(*loop_, reader_->Exists(path));
  const auto folder = co::Run(*loop_, reader_->Exists(TempDir()));

  ASSERT_TRUE(file);
  ASSERT_TRUE(folder);
  EXPECT_TRUE(*file);
  EXPECT_TRUE(*folder);
}

NOLINT_TEST_P(FileSourceReaderTest, ExistsIsFalseForAMissingFile)
{
  const auto exists
    = co::Run(*loop_, reader_->Exists(TempDir() / "missing.txt"));

  ASSERT_TRUE(exists);
  EXPECT_FALSE(*exists);
}

} // namespace
