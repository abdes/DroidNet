//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/Internal/LooseCookedIndexRegistry.h>

#include <utility>

#include <Oxygen/Base/Logging.h>

namespace oxygen::content::import {

auto LooseCookedIndexRegistry::NormalizeKey(
  const std::filesystem::path& cooked_root) const -> std::string
{
  return cooked_root.lexically_normal().string();
}

auto LooseCookedIndexRegistry::BeginSession(
  const std::filesystem::path& cooked_root,
  const std::optional<data::SourceKey>& source_key) -> void
{
  const auto key = NormalizeKey(cooked_root);
  std::scoped_lock lock(mutex_);

  auto& entry = entries_[key];
  if (!entry.writer) {
    entry.writer = std::make_unique<LooseCookedWriter>(cooked_root);
    if (source_key.has_value()) {
      entry.writer->SetSourceKey(source_key);
      entry.source_key = source_key;
    }
  } else if (source_key.has_value()) {
    if (!entry.source_key.has_value()) {
      entry.writer->SetSourceKey(source_key);
      entry.source_key = source_key;
    } else if (*entry.source_key != *source_key) {
      LOG_F(WARNING, "Ignoring mismatched source key for '{}'", key);
    }
  }

  ++entry.active_sessions;
  DLOG_F(
    INFO, "Session started for '{}' (count={})", key, entry.active_sessions);
}

auto LooseCookedIndexRegistry::RegisterExternalFile(
  const std::filesystem::path& cooked_root,
  const data::loose_cooked::v1::FileKind kind, std::string_view relpath) -> void
{
  const auto key = NormalizeKey(cooked_root);
  std::scoped_lock lock(mutex_);

  auto& entry = entries_[key];
  if (!entry.writer) {
    entry.writer = std::make_unique<LooseCookedWriter>(cooked_root);
  }

  entry.writer->RegisterExternalFile(kind, relpath);
  DLOG_F(INFO, "File '{}' kind={} registered for '{}'", std::string(relpath),
    static_cast<uint32_t>(kind), key);
}

auto LooseCookedIndexRegistry::RegisterExternalAssetDescriptor(
  const std::filesystem::path& cooked_root, const data::AssetKey& key,
  const data::AssetType asset_type, std::string_view virtual_path,
  std::string_view descriptor_relpath, const uint64_t descriptor_size,
  const std::optional<base::Sha256Digest>& descriptor_sha256) -> void
{
  const auto storage_key = NormalizeKey(cooked_root);
  std::scoped_lock lock(mutex_);

  auto& entry = entries_[storage_key];
  if (!entry.writer) {
    entry.writer = std::make_unique<LooseCookedWriter>(cooked_root);
  }

  entry.writer->RegisterExternalAssetDescriptor(key, asset_type, virtual_path,
    descriptor_relpath, descriptor_size, descriptor_sha256);
  DLOG_F(INFO, "Asset '{}' type={} relpath='{}' registered for '{}'",
    data::to_string(key), static_cast<uint32_t>(asset_type),
    std::string(descriptor_relpath), storage_key);
}

auto LooseCookedIndexRegistry::EndSession(
  const std::filesystem::path& cooked_root)
  -> std::optional<LooseCookedWriteResult>
{
  const auto key = NormalizeKey(cooked_root);
  std::unique_ptr<LooseCookedWriter> writer;
  uint32_t remaining = 0;
  {
    std::scoped_lock lock(mutex_);
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
      LOG_F(WARNING, "End session without start for '{}'", key);
      return std::nullopt;
    }

    if (it->second.active_sessions == 0) {
      LOG_F(WARNING, "Session count underflow for '{}'", key);
    } else {
      --it->second.active_sessions;
    }

    remaining = it->second.active_sessions;
    if (remaining == 0) {
      writer = std::move(it->second.writer);
      entries_.erase(it);
    } else {
      DLOG_F(INFO, "Session ended for '{}' (remaining={})", key, remaining);
      if (it->second.writer) {
        DLOG_F(
          INFO, "Incremental flush for '{}' (remaining={})", key, remaining);
        const auto result = it->second.writer->Finish();
        DLOG_F(INFO, "Incremental index flushed for '{}' assets={} files={}",
          key, result.assets.size(), result.files.size());
      }
      return std::nullopt;
    }
  }

  LOG_F(INFO, "Finalizing index for '{}'", key);
  auto result = writer->Finish();
  LOG_F(INFO, "Index finalized for '{}' assets={} files={}", key,
    result.assets.size(), result.files.size());
  return result;
}

} // namespace oxygen::content::import
