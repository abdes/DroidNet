//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>

#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Shared.h>

namespace oxygen::content::internal {

class InFlightOperationTable final {
public:
  using SharedVoidOp = co::Shared<co::Co<std::shared_ptr<void>>>;
  struct Stats final {
    uint64_t find_calls { 0 };
    uint64_t find_hits { 0 };
    uint64_t insert_calls { 0 };
    uint64_t erase_calls { 0 };
    uint64_t clear_calls { 0 };
    std::size_t active_type_buckets { 0 };
    std::size_t active_operations { 0 };
  };
  struct RequestMeta final {
    LoadPriority priority { LoadPriority::kDefault };
    LoadIntent intent { LoadIntent::kRuntime };
    uint64_t sequence { 0 };
  };

  auto Clear() -> void
  {
    ++stats_.clear_calls;
    table_.clear();
  }
  auto Find(TypeId type_id, uint64_t hash_key, const RequestMeta& request)
    -> std::optional<SharedVoidOp>
  {
    ++stats_.find_calls;
    auto type_it = table_.find(type_id);
    if (type_it == table_.end()) {
      return std::nullopt;
    }
    auto it = type_it->second.find(hash_key);
    if (it == type_it->second.end()) {
      return std::nullopt;
    }
    ++stats_.find_hits;
    it->second.request = MergeRequestMeta(it->second.request, request);
    return it->second.op;
  }
  auto GetRequestMeta(TypeId type_id, uint64_t hash_key) const
    -> std::optional<RequestMeta>
  {
    const auto type_it = table_.find(type_id);
    if (type_it == table_.end()) {
      return std::nullopt;
    }
    const auto it = type_it->second.find(hash_key);
    if (it == type_it->second.end()) {
      return std::nullopt;
    }
    return it->second.request;
  }
  auto InsertOrAssign(TypeId type_id, uint64_t hash_key, SharedVoidOp op,
    const RequestMeta& request) -> void
  {
    ++stats_.insert_calls;
    table_[type_id].insert_or_assign(hash_key,
      Entry {
        .op = std::move(op),
        .request = request,
      });
  }
  auto Erase(TypeId type_id, uint64_t hash_key) -> void
  {
    ++stats_.erase_calls;
    const auto type_it = table_.find(type_id);
    if (type_it == table_.end()) {
      return;
    }
    type_it->second.erase(hash_key);
    if (type_it->second.empty()) {
      table_.erase(type_it);
    }
  }
  [[nodiscard]] auto GetStats() const -> Stats
  {
    std::size_t active_operations = 0;
    for (const auto& [type_id, entries] : table_) {
      static_cast<void>(type_id);
      active_operations += entries.size();
    }
    auto snapshot = stats_;
    snapshot.active_type_buckets = table_.size();
    snapshot.active_operations = active_operations;
    return snapshot;
  }
  auto ResetStats() noexcept -> void { stats_ = {}; }

private:
  struct Entry final {
    SharedVoidOp op {};
    RequestMeta request {};
  };

  static auto MergeRequestMeta(
    const RequestMeta& existing, const RequestMeta& incoming) -> RequestMeta
  {
    if (static_cast<uint8_t>(incoming.priority)
      > static_cast<uint8_t>(existing.priority)) {
      return RequestMeta {
        .priority = incoming.priority,
        .intent = incoming.intent,
        .sequence = existing.sequence < incoming.sequence ? existing.sequence
                                                          : incoming.sequence,
      };
    }
    if (static_cast<uint8_t>(incoming.priority)
      < static_cast<uint8_t>(existing.priority)) {
      return existing;
    }
    return existing.sequence <= incoming.sequence ? existing : incoming;
  }
  std::unordered_map<TypeId, std::unordered_map<uint64_t, Entry>> table_;
  Stats stats_ {};
};

} // namespace oxygen::content::internal
