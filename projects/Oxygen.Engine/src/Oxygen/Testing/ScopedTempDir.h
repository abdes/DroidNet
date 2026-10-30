//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string_view>

#include <Oxygen/Base/Macros.h>

namespace oxygen::testing {

//! A unique directory for one test, removed with everything in it afterwards.
/*!
 The directory is `temp_directory_path()/oxygen-tests/<leaf>`, where the leaf
 is `pid-<pid>-<suite>.<test>-<counter>`, so tests running concurrently in
 different processes or configurations never share a directory. The
 destructor removes the directory, ignoring errors, and refuses to delete
 anything outside the tests root.

 ```cpp
 const auto dir = ScopedTempDir {};
 const auto path = dir.File("data/payload.bin");
 ```
*/
class ScopedTempDir final {
public:
  //! Creates the directory. Throws `std::filesystem::filesystem_error` when it
  //! cannot be created.
  ScopedTempDir();

  ~ScopedTempDir();

  OXYGEN_MAKE_NON_COPYABLE(ScopedTempDir)
  OXYGEN_MAKE_NON_MOVABLE(ScopedTempDir)

  //! The directory.
  [[nodiscard]] auto Path() const noexcept -> const std::filesystem::path&
  {
    return path_;
  }

  //! `Path() / relative`, with its parent directories created. The file
  //! itself is not created.
  [[nodiscard]] auto File(std::string_view relative) const
    -> std::filesystem::path;

private:
  std::filesystem::path path_;
};

} // namespace oxygen::testing
