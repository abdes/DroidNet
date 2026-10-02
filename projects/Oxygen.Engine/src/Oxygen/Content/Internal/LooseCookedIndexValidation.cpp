//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Internal/LooseCookedIndexImpl.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>

namespace oxygen::content::internal {

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
  files.reserve(
    key_to_asset_info_.size() + kind_to_file_.size() + auxiliary_files_.size());
  for (const auto& [key, asset] : key_to_asset_info_) {
    static_cast<void>(key);
    files.push_back({
      .relative_path = std::string(std::string_view(string_storage_)
          .substr(asset.descriptor_relpath_offset)
          .data()),
      .size = asset.descriptor_size,
      .sha256 = asset.descriptor_sha256,
      .kind = std::nullopt,
    });
  }
  const auto append_file = [&](const auto kind, const FileInfo& file) {
    files.push_back({
      .relative_path = std::string(
        std::string_view(string_storage_).substr(file.relpath_offset).data()),
      .size = file.size,
      .sha256 = file.sha256,
      .kind = kind,
    });
  };
  for (const auto& [kind, file] : kind_to_file_) {
    append_file(kind, file);
  }
  for (const auto& file : auxiliary_files_) {
    append_file(data::loose_cooked::FileKind::kAuxiliary, file);
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
      issues.push_back({ relative, lc::IntegrityFailure::kLinkedPath });
      remaining.erase(relative);
      continue;
    }
    if (std::filesystem::is_directory(status)) {
      continue;
    }
    if (!std::filesystem::is_regular_file(status)) {
      issues.push_back({ relative, lc::IntegrityFailure::kNotRegular });
      remaining.erase(relative);
      continue;
    }
    if (relative == "container.index.bin"
      || relative == data::loose_cooked::kGenerationLeaseFileName) {
      continue;
    }
    const auto found = remaining.find(relative);
    if (found == remaining.end()) {
      issues.push_back({ relative, lc::IntegrityFailure::kUnexpected });
      continue;
    }
    const auto& expected = *found->second;
    if (entry.file_size() != expected.size) {
      issues.push_back({ relative, lc::IntegrityFailure::kSizeMismatch });
    } else if (check == lc::IntegrityCheck::kFull
      && base::ComputeFileSha256(entry.path()) != expected.sha256) {
      issues.push_back({ relative, lc::IntegrityFailure::kDigestMismatch });
    }
    remaining.erase(found);
  }
  for (const auto& [relative, file] : remaining) {
    static_cast<void>(file);
    issues.push_back({ relative, lc::IntegrityFailure::kMissing });
  }
  std::ranges::sort(issues, {}, &lc::FileIntegrityIssue::relative_path);
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
  const auto reason = [&] {
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
    }
    return "Invalid loose cooked content: ";
  }();
  throw std::runtime_error(reason + first.relative_path);
}

} // namespace oxygen::content::internal
