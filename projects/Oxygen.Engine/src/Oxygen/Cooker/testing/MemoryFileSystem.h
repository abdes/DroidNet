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
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/RegionStream.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::cooker::testing {

//! A file system held in memory: mount tables, library facts, and pak builds
//! that read their sources and publish their outputs without touching disk.
/*!
 Paths compare by their lexically normal generic spelling, so a test names
 files by absolute paths under a root no test creates. Adding a file or a
 folder adds its parent folders. Nothing is a link, so a path's canonical
 spelling is its lexically normal one.
*/
class MemoryFileSystem final : public content::cooked::CookerStorage,
                               public content::import::IMountFileSystem {
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
    -> content::cooked::PathKind override
  {
    const auto key = Key(path);
    if (folders_.contains(key)) {
      return content::cooked::PathKind::kFolder;
    }
    return files_.contains(key) ? content::cooked::PathKind::kFile
                                : content::cooked::PathKind::kNone;
  }

  [[nodiscard]] auto Open(const std::filesystem::path& path) const
    -> Result<serio::AnyDevice> override
  {
    const auto it = files_.find(Key(path));
    if (it == files_.end()) {
      return ::oxygen::Err(
        std::make_error_code(std::errc::no_such_file_or_directory));
    }
    auto file = serio::MemoryFile::FromBytes(it->second);
    if (!file) {
      return ::oxygen::Err(file.error());
    }
    return ::oxygen::Ok(serio::AnyDevice(std::move(file).value()));
  }

  //! Makes publishing `path` fail with `error`, as a read-only or locked
  //! destination does.
  auto FailPublishing(const std::filesystem::path& path,
    const std::error_code error
    = std::make_error_code(std::errc::permission_denied)) -> void
  {
    unpublishable_.insert_or_assign(Key(path), error);
  }

  [[nodiscard]] auto Publish(const std::filesystem::path& path)
    -> Result<std::unique_ptr<content::cooked::Publication>> override
  {
    if (const auto it = unpublishable_.find(Key(path));
      it != unpublishable_.end()) {
      return ::oxygen::Err(it->second);
    }
    return ::oxygen::Ok(std::unique_ptr<content::cooked::Publication>(
      std::make_unique<Publication>(*this, path)));
  }

  [[nodiscard]] auto Remove(const std::filesystem::path& path)
    -> Result<void> override
  {
    files_.erase(Key(path));
    return {};
  }

  [[nodiscard]] auto RemoveEmptyFolder(const std::filesystem::path& path)
    -> Result<bool> override
  {
    const auto key = Key(path);
    if (!folders_.contains(key)) {
      return ::oxygen::Ok(false);
    }
    const auto prefix = key.ends_with('/') ? key : key + '/';
    const auto holds = [&prefix](const auto& paths) -> bool {
      const auto next = paths.lower_bound(prefix);
      return next != paths.end() && Path(*next).starts_with(prefix);
    };
    if (holds(folders_) || holds(files_)) {
      return ::oxygen::Ok(false);
    }
    folders_.erase(key);
    return ::oxygen::Ok(true);
  }

  //! Makes moving the file `from` fail with `error`, as a file another
  //! process holds open does.
  auto FailMoving(const std::filesystem::path& from,
    const std::error_code error
    = std::make_error_code(std::errc::permission_denied)) -> void
  {
    unmovable_.insert_or_assign(Key(from), error);
  }

  [[nodiscard]] auto Move(const std::filesystem::path& from,
    const std::filesystem::path& to) -> Result<void> override
  {
    const auto it = files_.find(Key(from));
    if (it == files_.end()) {
      return ::oxygen::Err(
        std::make_error_code(std::errc::no_such_file_or_directory));
    }
    const auto key = Key(to);
    if (folders_.contains(key)) {
      return ::oxygen::Err(std::make_error_code(std::errc::is_a_directory));
    }
    if (const auto parent = std::filesystem::path(key).parent_path();
      !parent.empty() && !folders_.contains(Key(parent))) {
      return ::oxygen::Err(
        std::make_error_code(std::errc::no_such_file_or_directory));
    }
    if (const auto failure = unmovable_.find(Key(from));
      failure != unmovable_.end()) {
      return ::oxygen::Err(failure->second);
    }
    auto bytes = std::move(it->second);
    files_.erase(it);
    files_.insert_or_assign(key, std::move(bytes));
    return {};
  }

  [[nodiscard]] auto SizeOf(const std::filesystem::path& path) const
    -> std::optional<serio::ByteCount> override
  {
    const auto it = files_.find(Key(path));
    if (it == files_.end()) {
      return std::nullopt;
    }
    return serio::ByteCount { it->second.size() };
  }

  //! The bytes of the file `path`; empty when there is none.
  [[nodiscard]] auto Bytes(const std::filesystem::path& path) const
    -> std::span<const std::byte>
  {
    const auto it = files_.find(Key(path));
    return it == files_.end() ? std::span<const std::byte> {} : it->second;
  }

  [[nodiscard]] auto Canonical(const std::filesystem::path& path) const
    -> std::filesystem::path override
  {
    return path.lexically_normal();
  }

private:
  //! Streams into memory; the file appears only when committed.
  class Publication final : public content::cooked::Publication {
  public:
    Publication(MemoryFileSystem& file_system, std::filesystem::path path)
      : file_system_(make_observer(&file_system))
      , path_(std::move(path))
      , stream_(file_)
    {
    }

    [[nodiscard]] auto Write(const std::span<const std::byte> in) noexcept
      -> Result<void> override
    {
      return stream_.Write(in);
    }

    [[nodiscard]] auto Flush() noexcept -> Result<void> override
    {
      return stream_.Flush();
    }

    [[nodiscard]] auto Position() const noexcept -> serio::ByteOffset override
    {
      return stream_.Position();
    }

    [[nodiscard]] auto PatchAt(const serio::ByteOffset at,
      const std::span<const std::byte> in) noexcept -> Result<void> override
    {
      return stream_.PatchAt(at, in);
    }

    [[nodiscard]] auto Commit() noexcept -> Result<void> override
    {
      CHECK_RESULT(stream_.Flush());
      file_system_->AddFile(path_, file_.Bytes());
      return {};
    }

  private:
    observer_ptr<MemoryFileSystem> file_system_;
    std::filesystem::path path_;
    // Keep this order: the stream writes `file_` and is destroyed first.
    serio::MemoryFile file_;
    serio::RegionWriteStream<serio::MemoryFile> stream_;
  };

  //! The key of a folder entry or a file entry.
  static auto Path(const std::string& folder) -> const std::string&
  {
    return folder;
  }

  template <typename Value>
  static auto Path(const std::pair<const std::string, Value>& file)
    -> const std::string&
  {
    return file.first;
  }

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
  std::map<std::string, std::error_code> unpublishable_;
  std::map<std::string, std::error_code> unmovable_;
};

} // namespace oxygen::cooker::testing
