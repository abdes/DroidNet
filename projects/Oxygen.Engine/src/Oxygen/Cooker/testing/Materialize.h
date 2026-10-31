//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

//! The one place the kit writes files.
/*!
 Builders work in memory. Only the tests of code that still reads the disk,
 such as `AssetLoader` mounting or the tools, write what a builder produced
 into a folder of an `oxygen::testing::ScopedTempDir`.
*/
namespace oxygen::cooker::testing {

//! Writes `files`, by path relative to `folder`, below `folder`.
/*!
 @throw std::system_error when a file cannot be written.
*/
auto Materialize(const std::map<std::string, std::vector<std::byte>>& files,
  const std::filesystem::path& folder) -> void;

//! Writes `bytes` as the file `path`, creating its folders.
/*!
 @throw std::system_error when the file cannot be written.
*/
auto Materialize(
  std::span<const std::byte> bytes, const std::filesystem::path& path) -> void;

} // namespace oxygen::cooker::testing
