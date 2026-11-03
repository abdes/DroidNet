//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Tools/PakTool/StagedSourceStorage.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::pak::tool {

namespace {
  //! `path` relative to `root`, when `path` lies inside `root`.
  auto RelativeInside(const std::filesystem::path& path,
    const std::filesystem::path& root) -> std::optional<std::filesystem::path>
  {
    const auto normal = path.lexically_normal();
    const auto base = root.lexically_normal();
    auto [inside, end] = std::ranges::mismatch(base, normal);
    if (inside != base.end()) {
      return std::nullopt;
    }
    auto relative = std::filesystem::path {};
    for (; end != normal.end(); ++end) {
      relative /= *end;
    }
    return relative;
  }
} // namespace

StagedSourceStorage::StagedSourceStorage(
  cooked::CookerStorage& storage) noexcept
  : storage_(make_observer(&storage))
{
}

auto StagedSourceStorage::Stage(const std::filesystem::path& staged,
  const std::filesystem::path& original) -> void
{
  staged_.push_back({ .staged = staged, .original = original });
}

auto StagedSourceStorage::OriginalOf(const std::filesystem::path& path) const
  -> std::optional<std::filesystem::path>
{
  for (const auto& root : staged_) {
    if (const auto relative = RelativeInside(path, root.staged)) {
      return relative->empty() ? root.original : root.original / *relative;
    }
  }
  return std::nullopt;
}

auto StagedSourceStorage::ReadPath(const std::filesystem::path& path) const
  -> std::filesystem::path
{
  if (storage_->KindOf(path) == cooked::PathKind::kFile) {
    return path;
  }
  return OriginalOf(path).value_or(path);
}

auto StagedSourceStorage::KindOf(const std::filesystem::path& path) const
  -> cooked::PathKind
{
  if (const auto kind = storage_->KindOf(path);
    kind != cooked::PathKind::kNone) {
    return kind;
  }
  const auto original = OriginalOf(path);
  return original ? storage_->KindOf(*original) : cooked::PathKind::kNone;
}

auto StagedSourceStorage::Open(const std::filesystem::path& path) const
  -> Result<serio::AnyDevice>
{
  return storage_->Open(ReadPath(path));
}

auto StagedSourceStorage::List(const std::filesystem::path& path) const
  -> Result<std::vector<content::lc::RootEntry>>
{
  return storage_->List(ReadPath(path));
}

auto StagedSourceStorage::SizeOf(const std::filesystem::path& path) const
  -> std::optional<serio::ByteCount>
{
  return storage_->SizeOf(ReadPath(path));
}

auto StagedSourceStorage::Publish(const std::filesystem::path& path)
  -> Result<std::unique_ptr<cooked::Publication>>
{
  return storage_->Publish(path);
}

auto StagedSourceStorage::TryLock(
  const std::filesystem::path& path, const serio::FileLockMode mode) const
  -> Result<std::unique_ptr<cooked::StorageLock>>
{
  return storage_->TryLock(path, mode);
}

auto StagedSourceStorage::Truncate(const std::filesystem::path& path,
  const serio::ByteCount size) -> Result<void>
{
  // A file the staged root only shows is its original's, which a staged
  // writer never cuts.
  if (storage_->KindOf(path) != cooked::PathKind::kFile) {
    return {};
  }
  return storage_->Truncate(path, size);
}

auto StagedSourceStorage::Remove(const std::filesystem::path& path)
  -> Result<void>
{
  return storage_->Remove(path);
}

auto StagedSourceStorage::RemoveEmptyFolder(const std::filesystem::path& path)
  -> Result<bool>
{
  return storage_->RemoveEmptyFolder(path);
}

auto StagedSourceStorage::Move(const std::filesystem::path& from,
  const std::filesystem::path& to) -> Result<void>
{
  return storage_->Move(from, to);
}

} // namespace oxygen::content::pak::tool
