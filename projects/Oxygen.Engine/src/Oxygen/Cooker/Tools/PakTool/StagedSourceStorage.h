//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::pak::tool {

//! The storage a pak build reads its sources from when some loose roots are
//! staged: a staged root holds only the files sealing changed, and every
//! other file is read from the root it stages.
/*!
 Inside a staged root, a path names the staged file when the staged root holds
 one, and the same path of the original root otherwise; a folder is one when
 either holds it. Writes go to the storage underneath, so they land in the
 staged root and never touch the original. Outside every staged root, the
 storage underneath answers.
*/
class StagedSourceStorage final : public cooked::CookerStorage {
public:
  explicit StagedSourceStorage(cooked::CookerStorage& storage) noexcept;

  //! Stages `original` at `staged`.
  auto Stage(const std::filesystem::path& staged,
    const std::filesystem::path& original) -> void;

  [[nodiscard]] auto KindOf(const std::filesystem::path& path) const
    -> cooked::PathKind override;
  [[nodiscard]] auto Open(const std::filesystem::path& path) const
    -> Result<serio::AnyDevice> override;
  [[nodiscard]] auto SizeOf(const std::filesystem::path& path) const
    -> std::optional<serio::ByteCount> override;

  [[nodiscard]] auto Publish(const std::filesystem::path& path)
    -> Result<std::unique_ptr<cooked::Publication>> override;
  [[nodiscard]] auto Remove(const std::filesystem::path& path)
    -> Result<void> override;
  [[nodiscard]] auto RemoveEmptyFolder(const std::filesystem::path& path)
    -> Result<bool> override;
  [[nodiscard]] auto Move(const std::filesystem::path& from,
    const std::filesystem::path& to) -> Result<void> override;

private:
  struct StagedRoot {
    std::filesystem::path staged;
    std::filesystem::path original;
  };

  //! The path the original root holds for `path` inside a staged root; none
  //! outside every staged root.
  [[nodiscard]] auto OriginalOf(const std::filesystem::path& path) const
    -> std::optional<std::filesystem::path>;

  //! `path`, or its original when the staged root holds no file there.
  [[nodiscard]] auto ReadPath(const std::filesystem::path& path) const
    -> std::filesystem::path;

  observer_ptr<cooked::CookerStorage> storage_;
  std::vector<StagedRoot> staged_;
};

} // namespace oxygen::content::pak::tool
