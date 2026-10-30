//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <system_error>

#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Base/SystemError.h>
#include <Oxygen/Testing/GTest.h>

#ifdef OXYGEN_WINDOWS
#  include <Windows.h> // IWYU pragma: keep

#  include <fileapi.h>
#  include <handleapi.h>
#  include <winerror.h>
#else
#  include <cerrno>

#  include <fcntl.h>
#endif

namespace {
using oxygen::base::LastSystemError;

//! Opening a missing file reports the OS error in the system category.
NOLINT_TEST(SystemErrorTest, FailedOpenReportsTheOsErrorInSystemCategory)
{
  const auto missing = std::filesystem::temp_directory_path()
    / "oxygen-absent-dir" / "oxygen-absent-file.bin";
#ifdef OXYGEN_WINDOWS
  auto* const handle = CreateFileW(missing.c_str(), GENERIC_READ, 0, nullptr,
    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  const auto error = LastSystemError();
  ASSERT_EQ(handle, INVALID_HANDLE_VALUE);
  const auto expected = ERROR_PATH_NOT_FOUND;
#else
  const auto descriptor = ::open(missing.c_str(), O_RDONLY);
  const auto error = LastSystemError();
  ASSERT_LT(descriptor, 0);
  const auto expected = ENOENT;
#endif

  EXPECT_EQ(error.category(), std::system_category());
  EXPECT_EQ(error.value(), static_cast<int>(expected));
  EXPECT_EQ(error, std::errc::no_such_file_or_directory);
}

} // namespace
