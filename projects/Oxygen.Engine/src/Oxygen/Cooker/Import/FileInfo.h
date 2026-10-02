//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>

namespace oxygen::content::import {

//! File metadata information.
struct FileInfo {
  //! File size in bytes.
  uint64_t size = 0;

  //! Last modification time.
  std::filesystem::file_time_type last_modified {};

  //! True if path is a directory.
  bool is_directory = false;

  //! True if path is a symbolic link.
  bool is_symlink = false;

  //! Metadata observations compare their returned values exactly.
  auto operator==(const FileInfo&) const -> bool = default;
};

} // namespace oxygen::content::import
