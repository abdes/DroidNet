//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

//! File helpers for tests. Every function throws `std::runtime_error` naming
//! the path on failure; GoogleTest reports the exception as a test failure.
//! Paths go through base::ToNativePath, so long Windows paths work.
namespace oxygen::cooker::test {

[[nodiscard]] auto ReadBytes(const std::filesystem::path& path)
  -> std::vector<std::byte>;

[[nodiscard]] auto ReadText(const std::filesystem::path& path) -> std::string;

//! Writes `bytes`, creating parent directories and truncating the file.
auto WriteBytes(
  const std::filesystem::path& path, std::span<const std::byte> bytes) -> void;

//! Writes `text`, creating parent directories and truncating the file.
auto WriteText(const std::filesystem::path& path, std::string_view text)
  -> void;

} // namespace oxygen::cooker::test
