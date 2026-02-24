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
  struct RequestMeta final {
    LoadPriority priority { LoadPriority::kDefault };
    LoadIntent intent { LoadIntent::kRuntime };
    uint64_t sequence { 0 };
  };

  auto Clear() -> void { table_.clear(); }
  auto Find(TypeId type_id, uint64_t hash_key, const RequestMeta& request)
    -> std::optional<SharedVoidOp>
  {
    auto type_it = table_.find(type_id);
    if (type_it == table_.end()) {
      return std::nullopt;
    }
    auto it = type_it->second.find(hash_key);
    if (it == type_it->second.end()) {
      return std::nullopt;
    }
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
    table_[type_id].insert_or_assign(hash_key,
      Entry {
        .op = std::move(op),
        .request = request,
      });
  }
  auto Erase(TypeId type_id, uint64_t hash_key) -> void
  {
    const auto type_it = table_.find(type_id);
    if (type_it == table_.end()) {
      return;
    }
    type_it->second.erase(hash_key);
    if (type_it->second.empty()) {
      table_.erase(type_it);
    }
  }

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
};

} // namespace oxygen::content::internal
