//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "DemoShell/Runtime/ContentMountApplier.h"
#include "DemoShell/Runtime/PathNormalization.h"
#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/UI/ContentMountList.h"

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Content/ContentMounts.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/CookedSource.h>

namespace oxygen::examples::ui {

namespace {
  auto SamePath(
    const std::filesystem::path& lhs, const std::filesystem::path& rhs) -> bool
  {
    return runtime::NormalizePath(lhs) == runtime::NormalizePath(rhs);
  }

  auto Exists(const std::filesystem::path& path) -> bool
  {
    std::error_code error;
    return std::filesystem::exists(base::ToNativePath(path), error) && !error;
  }

  //! Reads the summary of `row`, or marks why it cannot be mounted.
  auto ReadRow(CookedSourceRow& row) -> void
  {
    row.summary.reset();
    row.failures.clear();
    row.open_failure.reset();
    if (row.path.empty() || !Exists(row.path)) {
      row.state = SourceState::kMissing;
      if (row.error.empty() || row.origin != SourceOrigin::kImportRecord) {
        row.error = row.path.empty() ? "The record selects no generation"
                                     : "Not found: " + row.path.string();
      }
      return;
    }
    try {
      row.summary = content::cooked::ReadCookedSummary(
        content::cooked::RealCookerStorage(), row.path);
      row.state = SourceState::kPending;
      row.error.clear();
    } catch (const data::CookedFormatError& error) {
      row.state = SourceState::kUnreadable;
      row.open_failure = error.Failure();
      row.error = error.what();
    } catch (const std::exception& error) {
      row.state = SourceState::kUnreadable;
      row.error = error.what();
    }
  }

  auto KindOfMount(const data::CookedSourceKind kind)
    -> content::ContentMount::Kind
  {
    return kind == data::CookedSourceKind::kPak
      ? content::ContentMount::Kind::kPakFile
      : content::ContentMount::Kind::kLooseCookedRoot;
  }
} // namespace

auto to_string(const SourceState state) noexcept -> std::string_view
{
  switch (state) {
  case SourceState::kPending:
    return "Pending";
  case SourceState::kMounted:
    return "Mounted";
  case SourceState::kRefused:
    return "Refused";
  case SourceState::kMissing:
    return "Missing";
  case SourceState::kUnreadable:
    return "Unreadable";
  }
  return "Unknown";
}

auto MountNameOf(const CookedSourceRow& row) -> std::string
{
  if (!row.summary || row.summary->mounts.empty()) {
    return "?";
  }
  return "/" + row.summary->mounts.front();
}

auto SourceNameOf(const std::filesystem::path& path) -> std::string
{
  const auto& parent = path.parent_path().filename();
  if (path.has_extension() || parent.empty()) {
    return path.filename().generic_string();
  }
  return (parent / path.filename()).generic_string();
}

auto ReadSourceDetails(const std::filesystem::path& path) -> SourceDetails
{
  const auto content = content::cooked::CookedContent::Open(
    content::cooked::RealCookerStorage(), path);
  auto details = SourceDetails {};
  for (const auto& asset : content.Assets()) {
    details.assets.push_back({
      .key = asset.key,
      .virtual_path = asset.virtual_path,
      .type = asset.type,
    });
  }
  std::ranges::sort(details.assets, {}, &SourceAsset::virtual_path);
  if (const auto* catalog = content.Catalog()) {
    details.content_version = catalog->content_version;
  }
  return details;
}

auto ContentMountList::Added() const -> std::vector<ContentSourceSetting>
{
  auto added = std::vector<ContentSourceSetting> {};
  for (const auto& row : rows_) {
    if (row.origin == SourceOrigin::kAdded) {
      added.push_back({ .kind = row.kind, .path = row.path });
    }
  }
  return added;
}

auto ContentMountList::SetAdded(
  const std::vector<ContentSourceSetting>& sources) -> void
{
  std::erase_if(rows_, [](const CookedSourceRow& row) -> bool {
    return row.origin == SourceOrigin::kAdded;
  });
  // Inserted bottom first, each on top of the added rows.
  for (const auto& source : std::ranges::reverse_view(sources)) {
    static_cast<void>(Add(source));
  }
}

auto ContentMountList::Records() const -> std::vector<std::filesystem::path>
{
  auto records = std::vector<std::filesystem::path> {};
  for (const auto& row : rows_) {
    if (row.origin == SourceOrigin::kImportRecord) {
      records.push_back(row.import_record);
    }
  }
  return records;
}

auto ContentMountList::SetRecords(const std::vector<RecordGeneration>& records)
  -> void
{
  // A generation already listed keeps its row: it stays mounted, and only a
  // new generation waits for its mount.
  const auto is_record = [](const CookedSourceRow& row) -> bool {
    return row.origin == SourceOrigin::kImportRecord;
  };
  auto kept = std::vector<CookedSourceRow> {};
  std::ranges::copy_if(rows_, std::back_inserter(kept), is_record);
  std::erase_if(rows_, is_record);
  auto rows = std::vector<CookedSourceRow> {};
  for (const auto& record : records) {
    const auto same = std::ranges::find_if(
      kept, [&record](const CookedSourceRow& row) -> bool {
        return !record.generation.empty() && row.path == record.generation
          && row.import_record == record.record;
      });
    if (same != kept.end()) {
      rows.push_back(std::move(*same));
      continue;
    }
    rows.push_back({
      .kind = data::CookedSourceKind::kLooseCooked,
      .path = record.generation,
      .origin = SourceOrigin::kImportRecord,
      .import_record = record.record,
      .state = record.generation.empty() ? SourceState::kMissing
                                         : SourceState::kPending,
      .error = record.error,
    });
  }
  rows_.insert(rows_.begin(), std::make_move_iterator(rows.begin()),
    std::make_move_iterator(rows.end()));
}

auto ContentMountList::Add(const ContentSourceSetting& source) -> bool
{
  if (source.path.empty() || IndexOf(source.path)) {
    return false;
  }
  Insert({ .kind = source.kind, .path = source.path });
  return true;
}

auto ContentMountList::Remove(const std::size_t index) -> bool
{
  if (index >= rows_.size()
    || rows_.at(index).origin == SourceOrigin::kBuiltIn) {
    return false;
  }
  rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(index));
  return true;
}

auto ContentMountList::Move(const RowMove move) -> bool
{
  const auto [from, to] = move;
  const auto is_added = [this](const std::size_t index) -> bool {
    return index < rows_.size()
      && rows_.at(index).origin == SourceOrigin::kAdded;
  };
  if (!is_added(from) || !is_added(to)) {
    return false;
  }
  if (from == to) {
    return true;
  }
  auto row = std::move(rows_.at(from));
  rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(from));
  rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(to), std::move(row));
  return true;
}

auto ContentMountList::IndexOf(const std::filesystem::path& path) const
  -> std::optional<std::size_t>
{
  if (path.empty()) {
    return std::nullopt;
  }
  for (std::size_t index = 0; index < rows_.size(); ++index) {
    if (!rows_.at(index).path.empty() && SamePath(rows_.at(index).path, path)) {
      return index;
    }
  }
  return std::nullopt;
}

auto ContentMountList::Inspect() -> std::vector<content::ContentMount>
{
  auto mounts = std::vector<content::ContentMount> {};
  for (auto& row : std::ranges::reverse_view(rows_)) {
    if (row.origin == SourceOrigin::kBuiltIn) {
      continue;
    }
    ReadRow(row);
    if (row.state == SourceState::kPending) {
      mounts.push_back({ .kind = KindOfMount(row.kind), .path = row.path });
    }
  }
  return mounts;
}

auto ContentMountList::ApplyOutcome(const runtime::MountOutcome& outcome,
  const std::vector<content::IAssetLoader::MountedSourceEntry>& mounted) -> void
{
  for (auto& row : rows_) {
    if (row.origin == SourceOrigin::kBuiltIn
      || row.state != SourceState::kPending) {
      continue;
    }
    if (outcome.error) {
      row.error = *outcome.error;
      continue;
    }
    const auto refused = std::ranges::find_if(
      outcome.refused, [&row](const auto& refusal) -> bool {
        return SamePath(refusal.pak, row.path);
      });
    if (refused != outcome.refused.end()) {
      row.state = SourceState::kRefused;
      row.failures = refused->failures;
      row.error = refused->message;
      continue;
    }
    const auto is_mounted
      = std::ranges::any_of(outcome.mounted, [&row](const auto& mount) -> bool {
          return SamePath(mount.path, row.path);
        });
    if (is_mounted) {
      row.state = SourceState::kMounted;
    }
  }

  // The engine pak is the loader's, beneath the list.
  std::erase_if(rows_, [](const CookedSourceRow& row) -> bool {
    return row.origin == SourceOrigin::kBuiltIn;
  });
  for (const auto& source : mounted) {
    if (source.source_kind != content::IAssetLoader::ContentSourceKind::kPak
      || IndexOf(source.source_path)) {
      continue;
    }
    auto row = CookedSourceRow {
      .kind = data::CookedSourceKind::kPak,
      .path = source.source_path,
      .origin = SourceOrigin::kBuiltIn,
    };
    ReadRow(row);
    if (row.summary) {
      row.state = SourceState::kMounted;
    }
    rows_.push_back(std::move(row));
  }
}

auto ContentMountList::InspectRow(const std::size_t index) -> void
{
  if (index < rows_.size()) {
    ReadRow(rows_.at(index));
  }
}

auto ContentMountList::SetRowState(
  const std::size_t index, const SourceState state, std::string error) -> void
{
  if (index < rows_.size()) {
    rows_.at(index).state = state;
    rows_.at(index).error = std::move(error);
  }
}

auto ContentMountList::HasProblem() const -> bool
{
  return std::ranges::any_of(rows_, [](const CookedSourceRow& row) -> bool {
    return row.state == SourceState::kRefused
      || row.state == SourceState::kMissing
      || row.state == SourceState::kUnreadable;
  });
}

auto ContentMountList::Insert(CookedSourceRow row) -> void
{
  const auto at = FirstOf(row.origin);
  rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(at), std::move(row));
}

auto ContentMountList::FirstOf(const SourceOrigin origin) const -> std::size_t
{
  const auto found
    = std::ranges::find_if(rows_, [origin](const CookedSourceRow& row) -> bool {
        return static_cast<int>(row.origin) >= static_cast<int>(origin);
      });
  return static_cast<std::size_t>(std::distance(rows_.begin(), found));
}

} // namespace oxygen::examples::ui
