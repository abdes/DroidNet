//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "DemoShell/Runtime/ContentMountApplier.h"
#include "DemoShell/Services/ContentSettingsService.h"

#include <Oxygen/Content/ContentMounts.h>
#include <Oxygen/Content/DependencyRequirementError.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/CookedSource.h>

namespace oxygen::examples::ui {

//! Whether a cooked source of the mount list is mounted.
enum class SourceState : uint8_t {
  //! Not applied yet.
  kPending,
  kMounted,
  //! Its requirements are not met by the sources below it.
  kRefused,
  //! Its file or folder is gone.
  kMissing,
  //! It cannot be read as a cooked source.
  kUnreadable,
};

[[nodiscard]] auto to_string(SourceState state) noexcept -> std::string_view;

//! Why a cooked source is in the list.
enum class SourceOrigin : uint8_t {
  //! The developer added it.
  kAdded,
  //! It is the selected generation of a mounted model import record.
  kImportRecord,
  //! The engine pak, beneath every other source.
  kBuiltIn,
};

//! A row of the Mounts tab: one cooked source.
struct CookedSourceRow final {
  data::CookedSourceKind kind { data::CookedSourceKind::kLooseCooked };
  //! The pak file or the loose cooked root folder.
  std::filesystem::path path;
  SourceOrigin origin { SourceOrigin::kAdded };
  //! The record of a `kImportRecord` row.
  std::filesystem::path import_record;
  SourceState state { SourceState::kPending };
  //! What it provides, deletes and requires; absent while it cannot be read.
  std::optional<content::cooked::CookedSummary> summary;
  //! The failed requirements of a refused pak.
  std::vector<content::FailedRequirement> failures;
  //! Why it is refused, missing or unreadable.
  std::string error;
  std::optional<data::CookedOpenFailure> open_failure;
};

//! The row's mount, such as `/Showcase`; `?` before its summary is read.
[[nodiscard]] auto MountNameOf(const CookedSourceRow& row) -> std::string;

//! The short name of a cooked source: a pak's file name, such as
//! `showcase.pak`, or a loose root's parent and folder, such as
//! `.cooked/Showcase`.
[[nodiscard]] auto SourceNameOf(const std::filesystem::path& path)
  -> std::string;

//! An asset a cooked source holds, as the Mounts tab lists it.
struct SourceAsset final {
  data::AssetKey key;
  std::string virtual_path;
  data::AssetType type { data::AssetType::kUnknown };
};

//! What the Mounts tab's details read of a source beyond its summary.
struct SourceDetails final {
  std::vector<SourceAsset> assets;
  //! A pak's content version; none for a loose root.
  std::optional<std::uint16_t> content_version;
};

//! Reads the assets of the cooked source `path`, without loading them.
/*!
 @throw data::CookedOpenError When the source cannot be read.
*/
[[nodiscard]] auto ReadSourceDetails(const std::filesystem::path& path)
  -> SourceDetails;

//! A mounted model import record and its selected generation.
struct RecordGeneration final {
  std::filesystem::path record;
  //! The generation root; empty when the record selects none.
  std::filesystem::path generation;
  //! Why it has no generation.
  std::string error;
};

//! The cooked sources the panel mounts, in precedence order (the first row
//! wins): the import record generations, newest first, then the sources the
//! developer added, then the engine pak.
/*!
 The list is applied whole (`runtime::ApplyContentMounts`). `Inspect` reads
 every source's summary first, so a missing or unreadable source is shown as
 such and left out of the set; a refused pak keeps its row and its failed
 requirements until the list is applied again.
*/
class ContentMountList final {
public:
  [[nodiscard]] auto Rows() const noexcept
    -> const std::vector<CookedSourceRow>&
  {
    return rows_;
  }

  //! The sources the developer added, top first.
  [[nodiscard]] auto Added() const -> std::vector<ContentSourceSetting>;
  auto SetAdded(const std::vector<ContentSourceSetting>& sources) -> void;

  //! The mounted import records, newest first.
  [[nodiscard]] auto Records() const -> std::vector<std::filesystem::path>;
  auto SetRecords(const std::vector<RecordGeneration>& records) -> void;

  //! Adds `source` on top of the added sources; false when it is listed.
  auto Add(const ContentSourceSetting& source) -> bool;

  //! Removes the added source or import record of row `index`; false for the
  //! engine pak or an index out of range.
  auto Remove(std::size_t index) -> bool;

  //! A row moved to another row's place.
  struct RowMove final {
    std::size_t from { 0 };
    std::size_t to { 0 };
  };

  //! Moves the added source of row `from` to row `to`, among the added
  //! sources; false when either row is not an added source.
  auto Move(RowMove move) -> bool;

  [[nodiscard]] auto IndexOf(const std::filesystem::path& path) const
    -> std::optional<std::size_t>;

  //! The number the panel shows for row `index`: the bottom row is #1, so
  //! a row outranks every lower number.
  [[nodiscard]] auto NumberOf(const std::size_t index) const noexcept
    -> std::size_t
  {
    return rows_.size() - index;
  }

  //! Reads each row's summary and marks missing and unreadable sources.
  //! Returns the set to apply, lowest precedence first.
  auto Inspect() -> std::vector<content::ContentMount>;

  //! Records what applying the set did: the mounted rows, the refused paks
  //! with their failures, and the engine pak the loader mounts beneath them.
  auto ApplyOutcome(const runtime::MountOutcome& outcome,
    const std::vector<content::IAssetLoader::MountedSourceEntry>& mounted)
    -> void;

  //! Reads the summary of row `index` again, as `Inspect` does.
  auto InspectRow(std::size_t index) -> void;

  //! Sets the state of row `index`, such as a generation the host mounted;
  //! `error` says why it is not mounted.
  auto SetRowState(std::size_t index, SourceState state, std::string error)
    -> void;

  //! True when a row is refused, missing or unreadable.
  [[nodiscard]] auto HasProblem() const -> bool;

private:
  auto Insert(CookedSourceRow row) -> void;
  [[nodiscard]] auto FirstOf(SourceOrigin origin) const -> std::size_t;

  std::vector<CookedSourceRow> rows_;
};

} // namespace oxygen::examples::ui
