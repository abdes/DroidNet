//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/Operation.h>
#include <Oxygen/Serio/RegionStream.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::cooker::testing {

//! A file system held in memory: mount tables, library facts, and pak builds
//! that read their sources and publish their outputs without touching disk.
/*!
 Paths compare by their lexically normal generic spelling, so a test names
 files by absolute paths under a root no test creates. Adding a file or a
 folder adds its parent folders. A path's canonical spelling is its lexically
 normal one, with the links a test adds resolved.
*/
class MemoryFileSystem final : public content::cooked::ImportStorage,
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

  //! Makes `link` a link to the folder `target`, as a junction or a symbolic
  //! link is: a path below `link` has its canonical spelling below `target`.
  auto AddLink(const std::filesystem::path& link,
    const std::filesystem::path& target) -> void
  {
    AddFolder(std::filesystem::path(Key(link)).parent_path());
    links_.insert_or_assign(Key(link), Key(target));
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

  //! Every folder and file below `path`; memory holds no links.
  [[nodiscard]] auto List(const std::filesystem::path& path) const
    -> Result<std::vector<content::lc::RootEntry>> override
  {
    const auto key = Key(path);
    const auto prefix = key.ends_with('/') ? key : key + "/";
    auto entries = std::vector<content::lc::RootEntry> {};
    for (const auto& folder : folders_) {
      if (folder.starts_with(prefix)) {
        entries.push_back({
          .relative_path = folder.substr(prefix.size()),
          .kind = content::lc::RootEntryKind::kFolder,
          .size = 0,
        });
      }
    }
    for (const auto& [file, bytes] : files_) {
      if (file.starts_with(prefix)) {
        entries.push_back({
          .relative_path = file.substr(prefix.size()),
          .kind = content::lc::RootEntryKind::kFile,
          .size = bytes.size(),
        });
      }
    }
    for (const auto& file : HeldLockFiles()) {
      if (file.starts_with(prefix)) {
        entries.push_back({
          .relative_path = file.substr(prefix.size()),
          .kind = content::lc::RootEntryKind::kFile,
          .size = 0,
        });
      }
    }
    if (entries.empty() && !folders_.contains(key)) {
      return ::oxygen::Err(
        std::make_error_code(std::errc::no_such_file_or_directory));
    }
    return ::oxygen::Ok(std::move(entries));
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

  //! Stores `bytes` as the file `path`, as `Publish` and `Commit` do; the
  //! awaited write completes without suspending, so `driver` is not used.
  [[nodiscard]] auto PublishAsync(serio::IoDriver& /*driver*/,
    std::filesystem::path path, const std::span<const std::byte> bytes)
    -> co::Co<Result<void>> override
  {
    if (const auto it = unpublishable_.find(Key(path));
      it != unpublishable_.end()) {
      co_return ::oxygen::Err(it->second);
    }
    AddFile(path, bytes);
    co_return Result<void> {};
  }

  //! A device over the stored bytes of the file `path`, created empty when
  //! missing. Writes land in the file system, so `Open`, `SizeOf` and `Bytes`
  //! see them; the device must not outlive the file system.
  [[nodiscard]] auto OpenData(serio::IoDriver& /*driver*/,
    const std::filesystem::path& path) -> Result<serio::AnyAsyncDevice> override
  {
    if (!files_.contains(Key(path))) {
      AddFile(path, std::span<const std::byte> {});
    }
    return ::oxygen::Ok(serio::AnyAsyncDevice(DataFile(*this, Key(path))));
  }

  //! Makes writes through a device `OpenData` returned for `path` fail with
  //! `error`, as a full volume does.
  auto FailDataWrites(const std::filesystem::path& path,
    const std::error_code error
    = serio::make_error_code(serio::Error::kNoSpace)) -> void
  {
    unwritable_data_.insert_or_assign(Key(path), error);
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
    // The errors of `serio::MoveFileOver`.
    const auto it = files_.find(Key(from));
    if (it == files_.end()) {
      return ::oxygen::Err(folders_.contains(Key(from))
          ? serio::make_error_code(serio::Error::kIsDirectory)
          : serio::make_error_code(serio::Error::kNotFound));
    }
    const auto key = Key(to);
    if (folders_.contains(key)) {
      return ::oxygen::Err(serio::make_error_code(serio::Error::kIsDirectory));
    }
    if (const auto parent = std::filesystem::path(key).parent_path();
      !parent.empty() && !folders_.contains(Key(parent))) {
      return ::oxygen::Err(serio::make_error_code(serio::Error::kNotFound));
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

  //! An in-process lock per path, held without waiting: threads of a test
  //! exclude each other as processes do on the real file system. While it is
  //! held, its file is listed as an empty file, as the real file system
  //! creates and removes it.
  [[nodiscard]] auto TryLock(
    const std::filesystem::path& path, const serio::FileLockMode mode) const
    -> Result<std::unique_ptr<content::cooked::StorageLock>> override
  {
    const std::scoped_lock guard(locks_mutex_);
    auto& state = locks_[Key(path)];
    const auto exclusive = mode == serio::FileLockMode::kExclusive;
    if (state.exclusive || (exclusive && state.shared != 0)) {
      return ::oxygen::Err(
        std::make_error_code(std::errc::device_or_resource_busy));
    }
    if (exclusive) {
      state.exclusive = true;
    } else {
      ++state.shared;
    }
    return ::oxygen::Ok(std::unique_ptr<content::cooked::StorageLock>(
      std::make_unique<HeldLock>(*this, Key(path), exclusive)));
  }

  [[nodiscard]] auto Truncate(const std::filesystem::path& path,
    const serio::ByteCount size) -> Result<void> override
  {
    const auto it = files_.find(Key(path));
    if (it == files_.end()) {
      return ::oxygen::Err(
        std::make_error_code(std::errc::no_such_file_or_directory));
    }
    if (it->second.size() > size.get()) {
      it->second.resize(static_cast<size_t>(size.get()));
    }
    return {};
  }

  //! Whether a lock of `path` is held.
  [[nodiscard]] auto IsLocked(const std::filesystem::path& path) const -> bool
  {
    const std::scoped_lock guard(locks_mutex_);
    const auto it = locks_.find(Key(path));
    return it != locks_.end() && it->second.IsHeld();
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
    auto canonical = Key(path);
    // A link's target may itself lie below a link; each pass resolves one.
    for (auto pass = std::size_t { 0 }; pass <= links_.size(); ++pass) {
      const auto link = std::ranges::find_if(links_, [&](const auto& entry) {
        return canonical == entry.first
          || canonical.starts_with(entry.first + "/");
      });
      if (link == links_.end()) {
        break;
      }
      canonical = link->second + canonical.substr(link->first.size());
    }
    return std::filesystem::path(canonical).lexically_normal();
  }

private:
  //! The holders of one lock path.
  struct LockState final {
    uint32_t shared = 0;
    bool exclusive = false;

    [[nodiscard]] auto IsHeld() const noexcept -> bool
    {
      return exclusive || shared != 0;
    }
  };

  //! A held in-process lock of one path.
  class HeldLock final : public content::cooked::StorageLock {
  public:
    HeldLock(const MemoryFileSystem& file_system, std::string key,
      const bool exclusive) noexcept
      : file_system_(make_observer(&file_system))
      , key_(std::move(key))
      , exclusive_(exclusive)
    {
    }

    ~HeldLock() override
    {
      const std::scoped_lock guard(file_system_->locks_mutex_);
      auto& state = file_system_->locks_.at(key_);
      if (exclusive_) {
        state.exclusive = false;
      } else {
        --state.shared;
      }
    }

    OXYGEN_MAKE_NON_COPYABLE(HeldLock)
    OXYGEN_MAKE_NON_MOVABLE(HeldLock)

  private:
    observer_ptr<const MemoryFileSystem> file_system_;
    std::string key_;
    bool exclusive_ = false;
  };

  //! The lock files held now, as the empty files the real file system holds.
  [[nodiscard]] auto HeldLockFiles() const -> std::vector<std::string>
  {
    const std::scoped_lock guard(locks_mutex_);
    auto held = std::vector<std::string> {};
    for (const auto& [key, state] : locks_) {
      if (state.IsHeld() && !files_.contains(key)) {
        held.push_back(key);
      }
    }
    return held;
  }

  //! The device `OpenData` hands out: it reads and writes the stored bytes of
  //! one file, growing them as `serio::MemoryFile` does.
  class DataFile {
  public:
    DataFile(MemoryFileSystem& file_system, std::string key) noexcept
      : file_system_(make_observer(&file_system))
      , key_(std::move(key))
    {
    }

    [[nodiscard]] auto Size() const noexcept -> Result<serio::ByteCount>
    {
      return ::oxygen::Ok(serio::CountOf(Stored().size()));
    }

    [[nodiscard]] auto ReadAt(const serio::ByteOffset at,
      const std::span<std::byte> out) const noexcept -> Result<serio::ByteCount>
    {
      const auto stored = Stored();
      if (at.get() >= stored.size()) {
        return ::oxygen::Ok(serio::ByteCount { 0 });
      }
      const auto start = static_cast<size_t>(at.get());
      const auto count = std::min(out.size(), stored.size() - start);
      std::ranges::copy(stored.subspan(start, count), out.begin());
      return ::oxygen::Ok(serio::CountOf(count));
    }

    //! Writes all of `in` at `at`, growing the file; the gap reads as zero.
    [[nodiscard]] auto WriteAt(const serio::ByteOffset at,
      const std::span<const std::byte> in) noexcept -> Result<void>
    {
      if (const auto failure = file_system_->unwritable_data_.find(key_);
        failure != file_system_->unwritable_data_.end()) {
        return ::oxygen::Err(failure->second);
      }
      try {
        auto& stored = file_system_->files_[key_];
        const auto start = static_cast<size_t>(at.get());
        if (stored.size() < start + in.size()) {
          stored.resize(start + in.size());
        }
        std::ranges::copy(
          in, stored.begin() + static_cast<std::ptrdiff_t>(start));
        return {};
      } catch (const std::bad_alloc&) {
        return ::oxygen::Err(
          std::make_error_code(std::errc::not_enough_memory));
      }
    }

    [[nodiscard]] auto Reserve(const serio::ByteCount size) noexcept
      -> Result<void>
    {
      try {
        file_system_->files_[key_].reserve(static_cast<size_t>(size.get()));
        return {};
      } catch (const std::bad_alloc&) {
        return ::oxygen::Err(
          std::make_error_code(std::errc::not_enough_memory));
      }
    }

    [[nodiscard]] auto Flush(const serio::Durability /*durability*/) noexcept
      -> Result<void>
    {
      return {};
    }

    [[nodiscard]] auto AsyncReadAt(const serio::ByteOffset at,
      const std::span<std::byte> out) const noexcept -> serio::ReadOperation
    {
      return serio::ReadOperation::Ready(ReadAt(at, out));
    }

    [[nodiscard]] auto AsyncWriteAt(const serio::ByteOffset at,
      const std::span<const std::byte> in) noexcept -> serio::WriteOperation
    {
      return serio::WriteOperation::Ready(WriteAt(at, in));
    }

  private:
    [[nodiscard]] auto Stored() const noexcept -> std::span<const std::byte>
    {
      const auto it = file_system_->files_.find(key_);
      return it == file_system_->files_.end()
        ? std::span<const std::byte> {}
        : std::span<const std::byte>(it->second);
    }

    observer_ptr<MemoryFileSystem> file_system_;
    std::string key_;
  };

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
      try {
        file_system_->AddFile(path_, file_.Bytes());
      } catch (const std::bad_alloc&) {
        return ::oxygen::Err(std::errc::not_enough_memory);
      } catch (const std::system_error& error) {
        return ::oxygen::Err(error.code());
      } catch (const std::length_error&) {
        return ::oxygen::Err(std::errc::value_too_large);
      }
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
  static auto Path(const std::string& folder) -> std::string_view
  {
    return folder;
  }

  template <typename Value>
  static auto Path(const std::pair<const std::string, Value>& file)
    -> std::string_view
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
  std::map<std::string, std::string> links_;
  std::map<std::string, std::vector<std::byte>> files_;
  std::map<std::string, std::error_code> unpublishable_;
  std::map<std::string, std::error_code> unmovable_;
  std::map<std::string, std::error_code> unwritable_data_;
  //! Entries are never erased, so a held lock's state outlives it.
  mutable std::map<std::string, LockState> locks_;
  mutable std::mutex locks_mutex_;
};

} // namespace oxygen::cooker::testing
