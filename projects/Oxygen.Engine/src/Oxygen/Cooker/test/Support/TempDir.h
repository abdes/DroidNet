//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::cooker::test {

//! Unique directory under `temp_directory_path()/oxygen-cooker-tests`.
/*!
 The leaf is `pid-<pid>-<suite>.<test>-<counter>`, so concurrent runs of
 different configurations never share a directory. The destructor removes the
 directory, ignoring errors, and refuses to delete anything outside the tests
 root.
*/
class ScopedTempDir final {
public:
  ScopedTempDir();
  ~ScopedTempDir();

  OXYGEN_MAKE_NON_COPYABLE(ScopedTempDir)
  OXYGEN_MAKE_NON_MOVABLE(ScopedTempDir)

  [[nodiscard]] auto Path() const noexcept -> const std::filesystem::path&
  {
    return path_;
  }

private:
  std::filesystem::path path_;
};

//! Fixture owning one `ScopedTempDir` per test.
class TempDirTest : public ::testing::Test {
protected:
  [[nodiscard]] auto TempDir() const noexcept -> const std::filesystem::path&
  {
    return dir_.Path();
  }

  //! `TempDir() / relative`, creating the parent directories.
  [[nodiscard]] auto TempPath(std::string_view relative) const
    -> std::filesystem::path;

private:
  ScopedTempDir dir_;
};

} // namespace oxygen::cooker::test
