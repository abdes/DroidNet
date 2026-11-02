//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Content/Internal/LooseCookedIndexImpl.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/LooseCookedResources.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Integrity.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::internal {

namespace {
  //! Checks each resource of each data file against its table row: that it
  //! lies in its data file and, for `kFull`, that its bytes hash to the row's
  //! digest. A table or data file that already failed its own check is not
  //! read.
  auto CheckResources(const std::filesystem::path& root,
    const std::span<const lc::FileIntegrity> inventory,
    const lc::IntegrityCheck check, std::vector<lc::FileIntegrityIssue>& issues)
    -> void
  {
    const auto failed = [&](const std::string& relative) -> bool {
      return std::ranges::any_of(issues,
        [&](const auto& issue) { return issue.relative_path == relative; });
    };
    const auto find =
      [&](const data::loose_cooked::FileKind kind) -> const lc::FileIntegrity* {
      const auto it = std::ranges::find(
        inventory, std::optional { kind }, &lc::FileIntegrity::kind);
      return it == inventory.end() ? nullptr : std::addressof(*it);
    };
    for (const auto& table : inventory) {
      if (!table.kind.has_value()) {
        continue;
      }
      const auto data_kind = data::loose_cooked::DataFileKindOf(*table.kind);
      if (!data_kind.has_value()) {
        continue;
      }
      const auto* const data_file = find(*data_kind);
      const auto files_sound = data_file != nullptr
        && !failed(table.relative_path) && !failed(data_file->relative_path);
      if (!files_sound) {
        continue;
      }
      const auto table_path = base::ToNativePath(root / table.relative_path);
      const auto table_file = serio::NativeFile::Open(
        table_path, serio::OpenSpec { .hint = serio::AccessHint::kSequential });
      if (!table_file) {
        throw std::runtime_error("cannot open '" + table_path.string()
          + "': " + table_file.error().message());
      }
      const auto rows
        = serio::ReadAll(*table_file, serio::ByteCount { table.size });
      if (!rows) {
        throw std::runtime_error("cannot read '" + table_path.string()
          + "': " + rows.error().message());
      }
      auto payloads = std::vector<data::loose_cooked::ResourcePayload> {};
      try {
        payloads = data::loose_cooked::ReadResourcePayloads(
          *table.kind, rows->Bytes());
      } catch (const data::CookedOpenError&) {
        issues.push_back({
          .relative_path = table.relative_path,
          .reason = lc::IntegrityFailure::kMalformedTable,
          .resource_row = std::nullopt,
        });
        continue;
      }
      if (check == lc::IntegrityCheck::kMetadata) {
        // The data file has its indexed size, so bounds need no read.
        for (const auto& payload : payloads) {
          const auto in_data_file = payload.offset <= data_file->size
            && payload.size <= data_file->size - payload.offset;
          if (!in_data_file) {
            issues.push_back({
              .relative_path = data_file->relative_path,
              .reason = lc::IntegrityFailure::kResourceOutOfBounds,
              .resource_row = payload.row,
            });
          }
        }
        continue;
      }
      const auto data_path
        = base::ToNativePath(root / data_file->relative_path);
      const auto device = serio::NativeFile::Open(
        data_path, serio::OpenSpec { .hint = serio::AccessHint::kSequential });
      if (!device) {
        throw std::runtime_error("cannot open '" + data_path.string()
          + "': " + device.error().message());
      }
      for (const auto& failure :
        data::loose_cooked::VerifyResourcePayloads(payloads, *device)) {
        issues.push_back({
          .relative_path = data_file->relative_path,
          .reason = failure.reason
              == data::loose_cooked::ResourcePayloadFailure::kOutOfBounds
            ? lc::IntegrityFailure::kResourceOutOfBounds
            : lc::IntegrityFailure::kResourceDigestMismatch,
          .resource_row = failure.row,
        });
      }
    }
  }
} // namespace

auto LooseCookedIndexImpl::FindFileSha256(
  const data::loose_cooked::FileKind kind) const noexcept
  -> std::optional<std::span<const uint8_t, data::loose_cooked::kSha256Size>>
{
  const auto found = kind_to_file_.find(kind);
  if (found == kind_to_file_.end()) {
    return std::nullopt;
  }
  return std::span<const uint8_t, data::loose_cooked::kSha256Size>(
    found->second.sha256);
}

auto LooseCookedIndexImpl::GetFileInventory() const
  -> std::vector<lc::FileIntegrity>
{
  std::vector<lc::FileIntegrity> files;
  files.reserve(key_to_asset_info_.size() + kind_to_file_.size()
    + auxiliary_files_.size() + texture_descriptors_.size());
  // A string in the storage runs from its offset to its NUL terminator.
  const auto string_at = [this](const uint32_t offset) -> std::string {
    const auto rest = string_table_.substr(offset);
    return std::string(rest.substr(0, rest.find('\0')));
  };
  for (const auto& [key, asset] : key_to_asset_info_) {
    static_cast<void>(key);
    files.push_back({
      .relative_path = string_at(asset.descriptor_relpath_offset),
      .size = asset.descriptor_size,
      .sha256 = asset.descriptor_sha256,
      .kind = std::nullopt,
      .virtual_path = {},
      .source_path = {},
    });
  }
  const auto append_file = [&](const auto kind, const FileInfo& file) -> auto {
    files.push_back({
      .relative_path = string_at(file.relpath_offset),
      .size = file.size,
      .sha256 = file.sha256,
      .kind = kind,
      .virtual_path = string_at(file.virtual_path_offset),
      .source_path = string_at(file.source_path_offset),
    });
  };
  for (const auto& [kind, file] : kind_to_file_) {
    append_file(kind, file);
  }
  for (const auto& file : auxiliary_files_) {
    append_file(data::loose_cooked::FileKind::kAuxiliary, file);
  }
  for (const auto& file : texture_descriptors_) {
    append_file(data::loose_cooked::FileKind::kTextureDescriptor, file);
  }
  std::ranges::sort(files, {}, &lc::FileIntegrity::relative_path);
  return files;
}

auto LooseCookedIndexImpl::CheckContent(
  const std::filesystem::path& cooked_root,
  const lc::IntegrityCheck check) const -> std::vector<lc::FileIntegrityIssue>
{
  const auto inventory = GetFileInventory();
  std::unordered_map<std::string, const lc::FileIntegrity*> remaining;
  remaining.reserve(inventory.size());
  for (const auto& file : inventory) {
    remaining.emplace(file.relative_path, &file);
  }

  std::vector<lc::FileIntegrityIssue> issues;
  const auto root = base::ToNativePath(
    std::filesystem::canonical(base::ToNativePath(cooked_root)));
  auto iterator = std::filesystem::recursive_directory_iterator(root);
  const auto end = std::filesystem::recursive_directory_iterator {};
  for (; iterator != end; ++iterator) {
    const auto& entry = *iterator;
    const auto relative
      = entry.path().lexically_relative(root).generic_string();
    const auto status = entry.symlink_status();
    if (std::filesystem::is_symlink(status)
      || base::ToLogicalPath(
           std::filesystem::canonical(base::ToNativePath(entry.path())))
        != base::ToLogicalPath(entry.path()).lexically_normal()) {
      iterator.disable_recursion_pending();
      issues.push_back({
        .relative_path = relative,
        .reason = lc::IntegrityFailure::kLinkedPath,
      });
      remaining.erase(relative);
      continue;
    }
    if (std::filesystem::is_directory(status)) {
      continue;
    }
    if (!std::filesystem::is_regular_file(status)) {
      issues.push_back({
        .relative_path = relative,
        .reason = lc::IntegrityFailure::kNotRegular,
      });
      remaining.erase(relative);
      continue;
    }
    if (relative == oxygen::data::loose_cooked::kIndexFileName
      || relative == data::loose_cooked::kGenerationLeaseFileName
      || relative == data::loose_cooked::kIndexLockFileName) {
      continue;
    }
    const auto found = remaining.find(relative);
    if (found == remaining.end()) {
      issues.push_back({
        .relative_path = relative,
        .reason = lc::IntegrityFailure::kUnexpected,
      });
      continue;
    }
    const auto& expected = *found->second;
    if (entry.file_size() != expected.size) {
      issues.push_back({
        .relative_path = relative,
        .reason = lc::IntegrityFailure::kSizeMismatch,
      });
    } else if (check == lc::IntegrityCheck::kFull
      && !(expected.kind.has_value()
        && data::loose_cooked::IsResourceDataFile(*expected.kind))) {
      const auto digest = serio::ComputeFileSha256(entry.path());
      if (!digest) {
        throw std::runtime_error("cannot hash '" + entry.path().string()
          + "': " + digest.error().message());
      }
      if (*digest != expected.sha256) {
        issues.push_back({
          .relative_path = relative,
          .reason = lc::IntegrityFailure::kDigestMismatch,
        });
      }
    }
    remaining.erase(found);
  }
  for (const auto& [relative, file] : remaining) {
    static_cast<void>(file);
    issues.push_back({
      .relative_path = relative,
      .reason = lc::IntegrityFailure::kMissing,
      .resource_row = std::nullopt,
    });
  }
  CheckResources(root, inventory, check, issues);
  std::ranges::sort(issues, [](const auto& a, const auto& b) -> bool {
    return std::tie(a.relative_path, a.resource_row)
      < std::tie(b.relative_path, b.resource_row);
  });
  return issues;
}

auto LooseCookedIndexImpl::ValidateContent(
  const std::filesystem::path& cooked_root,
  const lc::IntegrityCheck check) const -> void
{
  const auto issues = CheckContent(cooked_root, check);
  if (issues.empty()) {
    return;
  }
  const auto& first = issues.front();
  const auto* const reason = [&] -> const char* {
    switch (first.reason) {
    case lc::IntegrityFailure::kMissing:
      return "Missing loose cooked file: ";
    case lc::IntegrityFailure::kUnexpected:
      return "Unindexed loose cooked file: ";
    case lc::IntegrityFailure::kSizeMismatch:
      return "Loose cooked file size mismatch: ";
    case lc::IntegrityFailure::kDigestMismatch:
      return "Loose cooked file SHA-256 mismatch: ";
    case lc::IntegrityFailure::kLinkedPath:
      return "Linked path in loose cooked content: ";
    case lc::IntegrityFailure::kNotRegular:
      return "Non-regular loose cooked content: ";
    case lc::IntegrityFailure::kMalformedTable:
      return "Malformed loose cooked resource table: ";
    case lc::IntegrityFailure::kResourceOutOfBounds:
      return "Loose cooked resource outside its data file: ";
    case lc::IntegrityFailure::kResourceDigestMismatch:
      return "Loose cooked resource SHA-256 mismatch: ";
    }
    return "Invalid loose cooked content: ";
  }();
  auto message = reason + first.relative_path;
  if (first.resource_row.has_value()) {
    message += " (row " + std::to_string(*first.resource_row) + ")";
  }
  throw lc::IntegrityError(first, message);
}

} // namespace oxygen::content::internal
