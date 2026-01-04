//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <system_error>

namespace oxygen::examples::common {

[[nodiscard]] inline auto FindWorkspaceRoot() -> std::filesystem::path
{
  std::error_code ec;
  auto dir = std::filesystem::current_path(ec);
  if (ec) {
    return {};
  }

  for (int i = 0; i < 12; ++i) {
    const auto cmake_lists = dir / "CMakeLists.txt";
    const auto src_oxygen = dir / "src" / "Oxygen";

    if (std::filesystem::exists(cmake_lists, ec)
      && std::filesystem::exists(src_oxygen, ec)
      && std::filesystem::is_directory(src_oxygen, ec)) {
      return dir;
    }

    if (!dir.has_parent_path()) {
      break;
    }
    const auto parent = dir.parent_path();
    if (parent == dir) {
      break;
    }
    dir = parent;
  }

  return std::filesystem::current_path();
}

} // namespace oxygen::examples::common
