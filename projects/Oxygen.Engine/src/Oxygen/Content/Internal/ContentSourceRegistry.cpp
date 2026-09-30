//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Internal/ContentSourceRegistry.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/SourceOrigin.h>

namespace oxygen::content::internal {

auto ContentSourceRegistry::FindSourceKeyConflict(
  const data::SourceKey source_key, const std::string_view mount_identity) const
  -> std::optional<MountResult::SourceKeyConflict>
{
  if (source_key.IsNil()) {
    return std::nullopt;
  }
  for (size_t index = 0; index < sources_.size(); ++index) {
    const auto& source = sources_.at(index);
    if (source && source->GetSourceKey() == source_key
      && source->DebugName() != mount_identity) {
      return MountResult::SourceKeyConflict {
        .source_key = source_key,
        .existing_source_id = source_ids_.at(index),
        .existing_mount_identity = std::string(source->DebugName()),
      };
    }
  }
  return std::nullopt;
}

auto ContentSourceRegistry::AllocateSourceId() -> data::SourceInstanceId
{
  static std::atomic<uint64_t> next { 1 };
  auto candidate = next.load(std::memory_order_relaxed);
  for (;;) {
    if (candidate == std::numeric_limits<uint64_t>::max()) {
      throw std::length_error("Content source identity namespace is exhausted");
    }
    if (next.compare_exchange_weak(
          candidate, candidate + 1, std::memory_order_relaxed)) {
      return data::SourceInstanceId { candidate };
    }
  }
}

auto ContentSourceRegistry::InstallSource(
  std::shared_ptr<IContentSource> source,
  const data::SourceInstanceId source_id,
  const std::optional<size_t> replaced_index, const SourceKind kind)
  -> MountResult
{
  if (kind != SourceKind::kGeneration) {
    const auto prior = source_key_to_ids_.find(source->GetSourceKey());
    if (prior != source_key_to_ids_.end()
      && std::ranges::any_of(prior->second, [this](const auto id) -> auto {
           const auto& record = records_.at(id);
           return record.kind == SourceKind::kGeneration
             && !record.source.expired();
         })) {
      throw std::invalid_argument("An immutable generation identity cannot be "
                                  "mounted as mutable content");
    }
  }
  const auto index = replaced_index.value_or(sources_.size());
  auto conflict
    = FindSourceKeyConflict(source->GetSourceKey(), source->DebugName());
  if (conflict
    && (kind == SourceKind::kGeneration
      || records_.at(conflict->existing_source_id).kind
        == SourceKind::kGeneration)) {
    throw std::invalid_argument(
      "An immutable generation SourceKey is already mounted elsewhere");
  }
  SourceRecord record {
    .key = source->GetSourceKey(),
    .mount_identity = std::string(source->DebugName()),
    .source = source,
    .kind = kind,
  };
  sources_.reserve(sources_.size() + 1);
  source_ids_.reserve(source_ids_.size() + 1);

  const auto source_key = source->GetSourceKey();
  bool committed = false;
  const auto rollback = Finally([&] -> void {
    if (!committed) {
      source_id_to_index_.erase(source_id);
      records_.erase(source_id);
      if (auto bucket = source_key_to_ids_.find(source_key);
        bucket != source_key_to_ids_.end()) {
        std::erase(bucket->second, source_id);
        if (bucket->second.empty()) {
          source_key_to_ids_.erase(bucket);
        }
      }
    }
  });
  source_id_to_index_.emplace(source_id, index);
  records_.emplace(source_id, std::move(record));
  source_key_to_ids_[source_key].push_back(source_id);
  if (replaced_index) {
    const auto prior_id = source_ids_.at(index);
    if (records_.at(prior_id).kind != SourceKind::kGeneration) {
      records_.at(prior_id).readable = false;
    }
    source_id_to_index_.erase(prior_id);
    sources_.at(index) = std::move(source);
    source_ids_.at(index) = source_id;
  } else {
    sources_.push_back(std::move(source));
    source_ids_.push_back(source_id);
  }
  committed = true;
  return {
    .action = replaced_index ? MountAction::kRefreshed : MountAction::kMounted,
    .source_id = source_id,
    .source_index = index,
    .source_key_conflict = std::move(conflict),
  };
}

auto ContentSourceRegistry::MountPak(std::filesystem::path normalized_path,
  std::shared_ptr<IContentSource> source) -> MountResult
{
  const auto existing = std::ranges::find(pak_paths_, normalized_path);
  if (existing != pak_paths_.end()) {
    const auto pak_index
      = static_cast<size_t>(std::distance(pak_paths_.begin(), existing));
    const auto index = source_id_to_index_.at(pak_source_ids_.at(pak_index));
    const auto id = AllocateSourceId();
    auto result = InstallSource(std::move(source), id, index, SourceKind::kPak);
    pak_source_ids_.at(pak_index) = id;
    return result;
  }
  pak_paths_.reserve(pak_paths_.size() + 1);
  pak_source_ids_.reserve(pak_source_ids_.size() + 1);
  const auto id = AllocateSourceId();
  auto result
    = InstallSource(std::move(source), id, std::nullopt, SourceKind::kPak);
  pak_paths_.push_back(std::move(normalized_path));
  pak_source_ids_.push_back(id);
  return result;
}

auto ContentSourceRegistry::PrepareReplacement(
  const std::span<const PreparedSource> sources) const -> ContentSourceRegistry
{
  auto candidate = *this;
  candidate.Clear();
  std::unordered_set<std::string> identities;
  identities.reserve(sources.size());
  for (const auto& item : sources) {
    if (!item.source || !identities.emplace(item.source->DebugName()).second) {
      throw std::invalid_argument(
        "A replacement mount set requires unique sources");
    }
    if (item.generation) {
      static_cast<void>(candidate.MountGeneration(item.source, std::nullopt));
    } else {
      static_cast<void>(
        candidate.MountLoose(item.source->DebugName(), item.source));
    }
  }
  return candidate;
}

auto ContentSourceRegistry::Swap(ContentSourceRegistry& other) noexcept -> void
{
  sources_.swap(other.sources_);
  source_ids_.swap(other.source_ids_);
  source_id_to_index_.swap(other.source_id_to_index_);
  tombstones_by_source_id_.swap(other.tombstones_by_source_id_);
  records_.swap(other.records_);
  source_key_to_ids_.swap(other.source_key_to_ids_);
  pak_paths_.swap(other.pak_paths_);
  pak_source_ids_.swap(other.pak_source_ids_);
}

auto ContentSourceRegistry::MountLoose(
  const std::string_view normalized_debug_name,
  std::shared_ptr<IContentSource> source) -> MountResult
{
  std::optional<size_t> replaced;
  for (size_t index = 0; index < sources_.size(); ++index) {
    if (records_.at(source_ids_.at(index)).kind != SourceKind::kPak
      && sources_.at(index)
      && sources_.at(index)->DebugName() == normalized_debug_name) {
      if (records_.at(source_ids_.at(index)).kind == SourceKind::kGeneration) {
        throw std::invalid_argument(
          "Immutable generations require the generation mount API");
      }
      replaced = index;
      break;
    }
  }
  return InstallSource(
    std::move(source), AllocateSourceId(), replaced, SourceKind::kLoose);
}

auto ContentSourceRegistry::MountGeneration(
  std::shared_ptr<IContentSource> source,
  const std::optional<data::SourceKey> replaces) -> MountResult
{
  const auto key = source->GetSourceKey();
  if (key.IsNil()) {
    throw std::invalid_argument(
      "A cooked generation requires a non-nil SourceKey");
  }
  std::optional<size_t> replaced;
  if (replaces) {
    for (size_t index = 0; index < sources_.size(); ++index) {
      if (sources_.at(index)->GetSourceKey() == *replaces
        && records_.at(source_ids_.at(index)).kind == SourceKind::kGeneration) {
        replaced = index;
        break;
      }
    }
    if (!replaced) {
      throw std::invalid_argument(
        "The replaced generation is not actively mounted");
    }
  }
  const auto identities = source_key_to_ids_.find(key);
  if (identities != source_key_to_ids_.end()) {
    for (const auto id : identities->second) {
      const auto& record = records_.at(id);
      auto retained = record.source.lock();
      if (!retained) {
        continue;
      }
      if (record.kind != SourceKind::kGeneration
        || record.mount_identity != source->DebugName()) {
        throw std::invalid_argument(
          "A generation SourceKey belongs to another source");
      }
      if (const auto active = source_id_to_index_.find(id);
        active != source_id_to_index_.end()) {
        if (replaced && *replaced != active->second) {
          throw std::invalid_argument(
            "A generation is already mounted at another priority");
        }
        return {
          .action = MountAction::kRefreshed,
          .source_id = id,
          .source_index = active->second,
          .source_key_conflict = {},
        };
      }
    }
  }
  const auto id = AllocateSourceId();
  return InstallSource(
    std::move(source), id, replaced, SourceKind::kGeneration);
}

auto ContentSourceRegistry::RemoveActiveSource(const size_t index) -> void
{
  source_id_to_index_.erase(source_ids_.at(index));
  sources_.erase(sources_.begin() + static_cast<std::ptrdiff_t>(index));
  source_ids_.erase(source_ids_.begin() + static_cast<std::ptrdiff_t>(index));
  for (size_t current = index; current < sources_.size(); ++current) {
    source_id_to_index_.at(source_ids_.at(current)) = current;
  }
}

auto ContentSourceRegistry::RetireGeneration(const data::SourceKey key) -> bool
{
  for (size_t index = 0; index < sources_.size(); ++index) {
    if (sources_.at(index)->GetSourceKey() == key
      && records_.at(source_ids_.at(index)).kind == SourceKind::kGeneration) {
      RemoveActiveSource(index);
      return true;
    }
  }
  return false;
}

auto ContentSourceRegistry::AcquireSource(
  const data::SourceInstanceId source_id) const
  -> std::shared_ptr<IContentSource>
{
  const auto found = records_.find(source_id);
  return found == records_.end() || !found->second.readable
    ? nullptr
    : found->second.source.lock();
}

auto ContentSourceRegistry::FindSourceIdByKey(const data::SourceKey key) const
  -> std::optional<data::SourceInstanceId>
{
  const auto identities = source_key_to_ids_.find(key);
  if (identities == source_key_to_ids_.end()) {
    return std::nullopt;
  }
  std::optional<data::SourceInstanceId> selected;
  for (const auto id : identities->second) {
    if (source_id_to_index_.contains(id)) {
      if (selected) {
        return std::nullopt;
      }
      selected = id;
    }
  }
  if (selected) {
    return selected;
  }
  for (auto id = identities->second.rbegin(); id != identities->second.rend();
    ++id) {
    if (records_.at(*id).readable && !records_.at(*id).source.expired()) {
      return *id;
    }
  }
  return std::nullopt;
}

auto ContentSourceRegistry::GetSourceKey(
  const data::SourceInstanceId source_id) const
  -> std::optional<data::SourceKey>
{
  const auto found = records_.find(source_id);
  return found == records_.end() ? std::nullopt
                                 : std::optional { found->second.key };
}

auto ContentSourceRegistry::IsKnownSource(
  const data::SourceInstanceId source_id) const -> bool
{
  return records_.contains(source_id);
}

auto ContentSourceRegistry::FindPakId(const std::filesystem::path& path) const
  -> std::optional<data::SourceInstanceId>
{
  const auto found = std::ranges::find(pak_paths_, path);
  return found == pak_paths_.end()
    ? std::nullopt
    : std::optional { pak_source_ids_.at(
        static_cast<size_t>(std::distance(pak_paths_.begin(), found))) };
}

auto ContentSourceRegistry::Clear() -> void
{
  for (const auto id : source_ids_) {
    if (records_.at(id).kind != SourceKind::kGeneration) {
      records_.at(id).readable = false;
    }
  }
  sources_.clear();
  source_ids_.clear();
  source_id_to_index_.clear();
  pak_paths_.clear();
  pak_source_ids_.clear();
}

auto ContentSourceRegistry::PruneExpiredSources()
  -> std::unordered_set<data::SourceInstanceId>
{
  std::unordered_set<data::SourceInstanceId> expired;
  for (const auto& [id, record] : records_) {
    if (!source_id_to_index_.contains(id) && record.source.expired()) {
      expired.insert(id);
    }
  }
  if (expired.empty()) {
    return expired;
  }
  for (auto bucket = source_key_to_ids_.begin();
    bucket != source_key_to_ids_.end();) {
    std::erase_if(bucket->second,
      [&expired](const auto id) { return expired.contains(id); });
    if (bucket->second.empty()) {
      bucket = source_key_to_ids_.erase(bucket);
    } else {
      ++bucket;
    }
  }
  for (const auto id : expired) {
    tombstones_by_source_id_.erase(id);
    records_.erase(id);
  }
  return expired;
}

auto ContentSourceRegistry::SetSourceTombstones(
  const data::SourceInstanceId source_id,
  const std::span<const data::AssetKey> tombstones) -> void
{
  if (!source_id_to_index_.contains(source_id)) {
    LOG_F(WARNING,
      "Ignoring tombstone registration for unknown source_id={} "
      "(tombstones={})",
      source_id, tombstones.size());
    return;
  }

  auto& source_tombstones = tombstones_by_source_id_[source_id];
  source_tombstones.clear();
  source_tombstones.insert(tombstones.begin(), tombstones.end());
}

auto ContentSourceRegistry::ClearSourceTombstones(
  const data::SourceInstanceId source_id) -> void
{
  tombstones_by_source_id_.erase(source_id);
}

auto ContentSourceRegistry::IsSourceTombstoningAsset(
  const data::SourceInstanceId source_id, const data::AssetKey& key) const
  -> bool
{
  if (const auto it = tombstones_by_source_id_.find(source_id);
    it != tombstones_by_source_id_.end()) {
    return it->second.contains(key);
  }
  return false;
}

auto ContentSourceRegistry::FindSourceIndexById(
  const data::SourceInstanceId source_id) const -> std::optional<size_t>
{
  if (const auto it = source_id_to_index_.find(source_id);
    it != source_id_to_index_.end()) {
    return it->second;
  }
  return std::nullopt;
}

auto ContentSourceRegistry::AssertStructuralConsistency(
  const std::string_view context) const -> void
{
#ifndef NDEBUG
  if (source_ids_.size() != sources_.size()) {
    LOG_F(ERROR,
      "[invariant:{}] source_ids/source vectors diverged: ids={} sources={}",
      context, source_ids_.size(), sources_.size());
  }

  for (const auto& [source_id, index] : source_id_to_index_) {
    if (index >= sources_.size()) {
      LOG_F(ERROR,
        "[invariant:{}] source_id_to_index out of range: source_id={} index={} "
        "sources={}",
        context, source_id, index, sources_.size());
      continue;
    }
    if (!sources_.at(index)) {
      LOG_F(ERROR,
        "[invariant:{}] source_id_to_index points to null source: source_id={} "
        "index={}",
        context, source_id, index);
    }
  }

  for (const auto& [source_id, tombstones] : tombstones_by_source_id_) {
    if (!records_.contains(source_id)) {
      LOG_F(ERROR,
        "[invariant:{}] tombstones mapped to unknown source_id: "
        "source_id={} tombstones={}",
        context, source_id, tombstones.size());
    }
  }
#else
  static_cast<void>(context);
#endif
}

} // namespace oxygen::content::internal
