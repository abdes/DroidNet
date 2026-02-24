//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Internal/ContentSourceRegistry.h>

namespace oxygen::content::internal {

auto ContentSourceRegistry::MountPak(std::filesystem::path normalized_path,
  std::unique_ptr<IContentSource> source) -> MountResult
{
  if (const auto it = std::ranges::find(pak_paths_, normalized_path);
    it != pak_paths_.end()) {
    const auto source_id
      = static_cast<uint16_t>(std::distance(pak_paths_.begin(), it));
    const auto source_index = source_id_to_index_.at(source_id);
    sources_[source_index] = std::move(source);
    return {
      .action = MountAction::kRefreshed,
      .source_id = source_id,
      .source_index = source_index,
    };
  }

  const auto source_id = static_cast<uint16_t>(pak_paths_.size());
  sources_.push_back(std::move(source));
  source_ids_.push_back(source_id);
  source_id_to_index_.insert_or_assign(source_id, sources_.size() - 1);

  const SourceToken token { next_source_token_value_++ };
  source_tokens_.push_back(token);
  token_to_source_id_.insert_or_assign(token, source_id);
  pak_paths_.push_back(std::move(normalized_path));

  return {
    .action = MountAction::kMounted,
    .source_id = source_id,
    .source_index = sources_.size() - 1,
  };
}

auto ContentSourceRegistry::MountLoose(std::string_view normalized_debug_name,
  std::unique_ptr<IContentSource> source) -> MountResult
{
  for (size_t source_index = 0; source_index < sources_.size();
    ++source_index) {
    if (!sources_[source_index]) {
      continue;
    }
    const auto source_id = source_ids_.at(source_index);
    if (source_id < constants::kLooseCookedSourceIdBase) {
      continue;
    }
    if (sources_[source_index]->DebugName() == normalized_debug_name) {
      sources_[source_index] = std::move(source);
      return {
        .action = MountAction::kRefreshed,
        .source_id = source_id,
        .source_index = source_index,
      };
    }
  }

  const auto source_id = next_loose_source_id_++;
  sources_.push_back(std::move(source));
  source_ids_.push_back(source_id);
  source_id_to_index_.insert_or_assign(source_id, sources_.size() - 1);

  const SourceToken token { next_source_token_value_++ };
  source_tokens_.push_back(token);
  token_to_source_id_.insert_or_assign(token, source_id);

  return {
    .action = MountAction::kMounted,
    .source_id = source_id,
    .source_index = sources_.size() - 1,
  };
}

auto ContentSourceRegistry::Clear() -> void
{
  sources_.clear();
  source_ids_.clear();
  source_id_to_index_.clear();
  source_tokens_.clear();
  token_to_source_id_.clear();
  next_source_token_value_ = 1;
  next_loose_source_id_ = constants::kLooseCookedSourceIdBase;
  pak_paths_.clear();
}

auto ContentSourceRegistry::FindSourceIdByToken(const SourceToken token) const
  -> std::optional<uint16_t>
{
  if (const auto it = token_to_source_id_.find(token);
    it != token_to_source_id_.end()) {
    return it->second;
  }
  return std::nullopt;
}

auto ContentSourceRegistry::FindSourceIndexById(const uint16_t source_id) const
  -> std::optional<size_t>
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
#if !defined(NDEBUG)
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
    if (!sources_[index]) {
      LOG_F(ERROR,
        "[invariant:{}] source_id_to_index points to null source: source_id={} "
        "index={}",
        context, source_id, index);
    }
  }
#else
  static_cast<void>(context);
#endif
}

} // namespace oxygen::content::internal
