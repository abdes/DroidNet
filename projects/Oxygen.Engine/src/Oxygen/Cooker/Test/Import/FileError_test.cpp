//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/FileError.cpp

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>

#include <Oxygen/Cooker/Import/FileError.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::content::import;

namespace {

//=== FileError Enum Tests
//===-----------------------------------------------//

NOLINT_TEST(FileErrorTest, kOkIsZero)
{
  EXPECT_EQ(static_cast<uint32_t>(FileError::kOk), 0U);
}

NOLINT_TEST(FileErrorTest, AllCodesAreDistinct)
{
  std::set<uint32_t> values;

  const auto insert_check = [&values](FileError code) -> void {
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

NOLINT_TEST(FileErrorInfoTest, IsErrorWithOkReturnsFalse)
{
  const FileErrorInfo info { .code = FileError::kOk };
  EXPECT_FALSE(info.IsError());
}

NOLINT_TEST(FileErrorInfoTest, IsErrorWithErrorReturnsTrue)
{
  const FileErrorInfo info { .code = FileError::kNotFound };
  EXPECT_TRUE(info.IsError());
}

NOLINT_TEST(FileErrorInfoTest, ToStringWithOkReturnsOk)
{
  const FileErrorInfo info { .code = FileError::kOk };
  EXPECT_EQ(info.ToString(), "OK");
}

NOLINT_TEST(FileErrorInfoTest, ToStringWithErrorIncludesName)
{
  const FileErrorInfo info { .code = FileError::kNotFound };
  const auto str = info.ToString();
  EXPECT_THAT(str, ::testing::HasSubstr("NotFound"));
}

NOLINT_TEST(FileErrorInfoTest, ToStringWithPathIncludesPath)
{
  const FileErrorInfo info {
    .code = FileError::kNotFound,
    .path = "/some/file.txt",
  };
  const auto str = info.ToString();
  EXPECT_THAT(str, ::testing::HasSubstr("/some/file.txt"));
}

NOLINT_TEST(FileErrorInfoTest, ToStringWithMessageIncludesMessage)
{
  const FileErrorInfo info {
    .code = FileError::kAccessDenied,
    .message = "Custom error message",
  };
  const auto str = info.ToString();
  EXPECT_THAT(str, ::testing::HasSubstr("Custom error message"));
}

NOLINT_TEST(FileErrorInfoTest, ToStringWithSystemErrorIncludesSystemError)
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

struct MapSystemErrorCase final {
  const char* name;
  std::error_code ec;
  FileError expected;
};

class MapSystemErrorTest : public ::testing::TestWithParam<MapSystemErrorCase> {
};

NOLINT_TEST_P(MapSystemErrorTest, MapsToExpectedFileError)
{
  EXPECT_EQ(MapSystemError(GetParam().ec), GetParam().expected);
}

INSTANTIATE_TEST_SUITE_P(KnownCodes, MapSystemErrorTest,
  ::testing::Values(MapSystemErrorCase { "NoErrorMapsToOk", std::error_code {},
                      FileError::kOk },
    MapSystemErrorCase { "NoSuchFileMapsToNotFound",
      std::make_error_code(std::errc::no_such_file_or_directory),
      FileError::kNotFound },
    MapSystemErrorCase { "PermissionDeniedMapsToAccessDenied",
      std::make_error_code(std::errc::permission_denied),
      FileError::kAccessDenied },
    MapSystemErrorCase { "FileExistsMapsToAlreadyExists",
      std::make_error_code(std::errc::file_exists), FileError::kAlreadyExists },
    MapSystemErrorCase { "IsDirectoryMapsToIsDirectory",
      std::make_error_code(std::errc::is_a_directory),
      FileError::kIsDirectory },
    MapSystemErrorCase { "NotDirectoryMapsToNotDirectory",
      std::make_error_code(std::errc::not_a_directory),
      FileError::kNotDirectory },
    MapSystemErrorCase { "TooManyFilesMapsToTooManyOpenFiles",
      std::make_error_code(std::errc::too_many_files_open),
      FileError::kTooManyOpenFiles },
    MapSystemErrorCase { "NoSpaceMapsToNoSpace",
      std::make_error_code(std::errc::no_space_on_device),
      FileError::kNoSpace },
    MapSystemErrorCase { "CancelledMapsToCancelled",
      std::make_error_code(std::errc::operation_canceled),
      FileError::kCancelled },
    // An uncommon error without an explicit mapping.
    MapSystemErrorCase { "UnknownErrorMapsToUnknown",
      std::make_error_code(std::errc::address_in_use), FileError::kUnknown }),
  [](const ::testing::TestParamInfo<MapSystemErrorCase>& info) -> std::string {
    return std::string(info.param.name);
  });

//=== MakeFileError Tests
//===--------------------------------------------//

NOLINT_TEST(MakeFileErrorTest, FromSystemErrorCreatesCorrectInfo)
{
  const std::filesystem::path path = "/test/file.txt";
  const auto ec = std::make_error_code(std::errc::no_such_file_or_directory);

  const auto info = MakeFileError(path, ec);

  EXPECT_EQ(info.code, FileError::kNotFound);
  EXPECT_EQ(info.path, path);
  EXPECT_EQ(info.system_error, ec);
  EXPECT_FALSE(info.message.empty());
}

NOLINT_TEST(MakeFileErrorTest, WithCustomMessageCreatesCorrectInfo)
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
