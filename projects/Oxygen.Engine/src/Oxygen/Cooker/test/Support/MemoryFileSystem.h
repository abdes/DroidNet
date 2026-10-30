//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/Loose/LibraryFacts.h>

namespace oxygen::cooker::test {

//! A file system held in memory, for mount table and library facts tests.
/*!
 Paths compare by their lexically normal generic spelling, so a test names
 files by absolute paths under a root no test creates. Adding a file or a
 folder adds its parent folders. Nothing is a link, so a path's canonical
 spelling is its lexically normal one.
*/
class MemoryFileSystem final : public content::import::IMountFileSystem {
public:
  //! Adds a folder and its parents.
  auto AddFolder(const std::filesystem::path& folder) -> void
  {
    for (auto path = Key(folder); !path.empty();) {
      if (!folders_.insert(path).second) {
        return;
      }
      const auto parent = std::filesystem::path(path).parent_path();
      if (parent.generic_string() == path) {
        return;
      }
      path = parent.generic_string();
    }
  }

  //! Adds a file, replacing one at the same path, and its parent folders.
  auto AddFile(const std::filesystem::path& file,
    const std::span<const std::byte> bytes) -> void
  {
    AddFolder(std::filesystem::path(Key(file)).parent_path());
    files_.insert_or_assign(
      Key(file), std::vector<std::byte>(bytes.begin(), bytes.end()));
  }

  //! Adds a text file.
  auto AddFile(const std::filesystem::path& file, const std::string_view text)
    -> void
  {
    AddFile(file, std::as_bytes(std::span(text)));
  }

  //! Adds every file below `root`, by path relative to it.
  auto AddFiles(const std::filesystem::path& root,
    const std::map<std::string, std::vector<std::byte>>& files) -> void
  {
    AddFolder(root);
    for (const auto& [relpath, bytes] : files) {
      AddFile(root / relpath, bytes);
    }
  }

  [[nodiscard]] auto KindOf(const std::filesystem::path& path) const
    -> content::lc::PathKind override
  {
    const auto key = Key(path);
    if (folders_.contains(key)) {
      return content::lc::PathKind::kFolder;
    }
    return files_.contains(key) ? content::lc::PathKind::kFile
                                : content::lc::PathKind::kNone;
  }

  [[nodiscard]] auto SizeOf(const std::filesystem::path& path) const
    -> std::optional<uint64_t> override
  {
    const auto it = files_.find(Key(path));
    if (it == files_.end()) {
      return std::nullopt;
    }
    return it->second.size();
  }

  [[nodiscard]] auto Read(
    const std::filesystem::path& path, const content::lc::ByteRange range) const
    -> std::optional<std::vector<std::byte>> override
  {
    const auto it = files_.find(Key(path));
    if (it == files_.end() || range.offset > it->second.size()
      || range.size > it->second.size() - range.offset) {
      return std::nullopt;
    }
    const auto bytes = std::span(it->second)
                         .subspan(static_cast<size_t>(range.offset),
                           static_cast<size_t>(range.size));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
  }

  [[nodiscard]] auto Canonical(const std::filesystem::path& path) const
    -> std::filesystem::path override
  {
    return path.lexically_normal();
  }

private:
  static auto Key(const std::filesystem::path& path) -> std::string
  {
    auto key = path.lexically_normal().generic_string();
    if (key.size() > 1 && key.ends_with('/') && !key.ends_with(":/")) {
      key.pop_back();
    }
    return key;
  }

  std::set<std::string> folders_;
  std::map<std::string, std::vector<std::byte>> files_;
};

} // namespace oxygen::cooker::test
