//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/FileError.h>

using namespace oxygen::content::import;

namespace {

//=== FileError Enum Tests
//===-----------------------------------------------//

class FileErrorTest : public ::testing::Test { };

//! Verify kOk is zero for boolean-like checks.
NOLINT_TEST_F(FileErrorTest, kOkIsZero)
{
  EXPECT_EQ(static_cast<uint32_t>(FileError::kOk), 0u);
}

//! Verify all error codes have distinct values.
NOLINT_TEST_F(FileErrorTest, AllCodesAreDistinct)
{
  std::set<uint32_t> values;

  const auto insert_check = [&values](FileError code) {
    auto [_, inserted] = values.insert(static_cast<uint32_t>(code));
    EXPECT_TRUE(inserted) << "Duplicate value for code "
                          << static_cast<uint32_t>(code);
  };

  insert_check(FileError::kOk);
  insert_check(FileError::kNotFound);
  insert_check(FileError::kAccessDenied);
  insert_check(FileError::kAlreadyExists);
  insert_check(FileError::kIsDirectory);
  insert_check(FileError::kNotDirectory);
  insert_check(FileError::kTooManyOpenFiles);
  insert_check(FileError::kNoSpace);
  insert_check(FileError::kDiskFull);
  insert_check(FileError::kReadOnly);
  insert_check(FileError::kInvalidPath);
  insert_check(FileError::kPathTooLong);
  insert_check(FileError::kIOError);
  insert_check(FileError::kCancelled);
  insert_check(FileError::kUnknown);
}

//=== FileErrorInfo Tests
//===---------------------------------------------//

class FileErrorInfoTest : public testing::Test { };

//! Verify IsError returns false for kOk.
NOLINT_TEST_F(FileErrorInfoTest, IsErrorWithOkReturnsFalse)
{
  const FileErrorInfo info { .code = FileError::kOk };
  EXPECT_FALSE(info.IsError());
}

//! Verify IsError returns true for error codes.
NOLINT_TEST_F(FileErrorInfoTest, IsErrorWithErrorReturnsTrue)
{
  const FileErrorInfo info { .code = FileError::kNotFound };
  EXPECT_TRUE(info.IsError());
}

//! Verify ToString with kOk returns "OK".
NOLINT_TEST_F(FileErrorInfoTest, ToStringWithOkReturnsOk)
{
  const FileErrorInfo info { .code = FileError::kOk };
  EXPECT_EQ(info.ToString(), "OK");
}

//! Verify ToString includes error name.
NOLINT_TEST_F(FileErrorInfoTest, ToStringWithErrorIncludesName)
{
  const FileErrorInfo info { .code = FileError::kNotFound };
  const auto str = info.ToString();
  EXPECT_THAT(str, ::testing::HasSubstr("NotFound"));
}

//! Verify ToString includes path when present.
NOLINT_TEST_F(FileErrorInfoTest, ToStringWithPathIncludesPath)
{
  const FileErrorInfo info {
    .code = FileError::kNotFound,
    .path = "/some/file.txt",
  };
  const auto str = info.ToString();
  EXPECT_THAT(str, ::testing::HasSubstr("/some/file.txt"));
}

//! Verify ToString includes message when present.
NOLINT_TEST_F(FileErrorInfoTest, ToStringWithMessageIncludesMessage)
{
  const FileErrorInfo info {
    .code = FileError::kAccessDenied,
    .message = "Custom error message",
  };
  const auto str = info.ToString();
  EXPECT_THAT(str, ::testing::HasSubstr("Custom error message"));
}

//! Verify ToString includes system error when present.
NOLINT_TEST_F(FileErrorInfoTest, ToStringWithSystemErrorIncludesSystemError)
{
  const FileErrorInfo info {
    .code = FileError::kNotFound,
    .system_error = std::make_error_code(std::errc::no_such_file_or_directory),
  };
  const auto str = info.ToString();
  EXPECT_THAT(str, ::testing::HasSubstr("system:"));
}

//=== MapSystemError Tests
//===--------------------------------------------//

class MapSystemErrorTest : public testing::Test { };

//! Verify no error maps to kOk.
NOLINT_TEST_F(MapSystemErrorTest, NoErrorMapsToOk)
{
  const std::error_code ec {};
  EXPECT_EQ(MapSystemError(ec), FileError::kOk);
}

//! Verify no_such_file_or_directory maps to kNotFound.
NOLINT_TEST_F(MapSystemErrorTest, NoSuchFileMapsToNotFound)
{
  const auto ec = std::make_error_code(std::errc::no_such_file_or_directory);
  EXPECT_EQ(MapSystemError(ec), FileError::kNotFound);
}

//! Verify permission_denied maps to kAccessDenied.
NOLINT_TEST_F(MapSystemErrorTest, PermissionDeniedMapsToAccessDenied)
{
  const auto ec = std::make_error_code(std::errc::permission_denied);
  EXPECT_EQ(MapSystemError(ec), FileError::kAccessDenied);
}

//! Verify file_exists maps to kAlreadyExists.
NOLINT_TEST_F(MapSystemErrorTest, FileExistsMapsToAlreadyExists)
{
  const auto ec = std::make_error_code(std::errc::file_exists);
  EXPECT_EQ(MapSystemError(ec), FileError::kAlreadyExists);
}

//! Verify is_a_directory maps to kIsDirectory.
NOLINT_TEST_F(MapSystemErrorTest, IsDirectoryMapsToIsDirectory)
{
  const auto ec = std::make_error_code(std::errc::is_a_directory);
  EXPECT_EQ(MapSystemError(ec), FileError::kIsDirectory);
}

//! Verify not_a_directory maps to kNotDirectory.
NOLINT_TEST_F(MapSystemErrorTest, NotDirectoryMapsToNotDirectory)
{
  const auto ec = std::make_error_code(std::errc::not_a_directory);
  EXPECT_EQ(MapSystemError(ec), FileError::kNotDirectory);
}

//! Verify too_many_files_open maps to kTooManyOpenFiles.
NOLINT_TEST_F(MapSystemErrorTest, TooManyFilesMapsToTooManyOpenFiles)
{
  const auto ec = std::make_error_code(std::errc::too_many_files_open);
  EXPECT_EQ(MapSystemError(ec), FileError::kTooManyOpenFiles);
}

//! Verify no_space_on_device maps to kNoSpace.
NOLINT_TEST_F(MapSystemErrorTest, NoSpaceMapsToNoSpace)
{
  const auto ec = std::make_error_code(std::errc::no_space_on_device);
  EXPECT_EQ(MapSystemError(ec), FileError::kNoSpace);
}

//! Verify operation_canceled maps to kCancelled.
NOLINT_TEST_F(MapSystemErrorTest, CancelledMapsToCancelled)
{
  const auto ec = std::make_error_code(std::errc::operation_canceled);
  EXPECT_EQ(MapSystemError(ec), FileError::kCancelled);
}

//! Verify unknown errors map to kUnknown.
NOLINT_TEST_F(MapSystemErrorTest, UnknownErrorMapsToUnknown)
{
  // Use an uncommon error that doesn't have explicit mapping
  const auto ec = std::make_error_code(std::errc::address_in_use);
  EXPECT_EQ(MapSystemError(ec), FileError::kUnknown);
}

//=== MakeFileError Tests
//===--------------------------------------------//

class MakeFileErrorTest : public testing::Test { };

//! Verify MakeFileError from system error creates correct info.
NOLINT_TEST_F(MakeFileErrorTest, FromSystemErrorCreatesCorrectInfo)
{
  const std::filesystem::path path = "/test/file.txt";
  const auto ec = std::make_error_code(std::errc::no_such_file_or_directory);

  const auto info = MakeFileError(path, ec);

  EXPECT_EQ(info.code, FileError::kNotFound);
  EXPECT_EQ(info.path, path);
  EXPECT_EQ(info.system_error, ec);
  EXPECT_FALSE(info.message.empty());
}

//! Verify MakeFileError with custom message creates correct info.
NOLINT_TEST_F(MakeFileErrorTest, WithCustomMessageCreatesCorrectInfo)
{
  const std::filesystem::path path = "/test/file.txt";
  constexpr auto code = FileError::kInvalidPath;
  const std::string message = "Path contains invalid characters";

  const auto info = MakeFileError(path, code, message);

  EXPECT_EQ(info.code, code);
  EXPECT_EQ(info.path, path);
  EXPECT_FALSE(info.system_error);
  EXPECT_EQ(info.message, message);
}

} // namespace
