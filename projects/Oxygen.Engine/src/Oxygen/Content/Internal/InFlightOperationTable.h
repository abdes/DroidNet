//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Content/Internal/ContentPublication.h>
#include <Oxygen/Content/OperationCancelledException.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/OxCo/Awaitables.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/ParkingLot.h>
#include <Oxygen/OxCo/Shared.h>

namespace oxygen::content::internal {

//! Run an in-flight operation to its end, whatever its waiters do.
/*!
 A waiter may stop waiting; the operation it joined does not stop. A
 cancelled coroutine runs its cleanup only when its frame is destroyed, and the
 table keeps that frame alive, so a cancellable operation would never release
 its key. Shielded, it always completes or fails, then leaves the table.
*/
inline auto RunToCompletion(co::Co<SharedContentResult> operation)
  -> co::Co<SharedContentResult>
{
  co_return co_await co::NonCancellable(std::move(operation));
}

class InFlightOperationTable final {
public:
  InFlightOperationTable() = default;
  ~InFlightOperationTable() { Close(); }
  OXYGEN_MAKE_NON_COPYABLE(InFlightOperationTable)
  OXYGEN_MAKE_NON_MOVABLE(InFlightOperationTable)

  using OperationId
    = NamedType<uint64_t, struct InFlightOperationIdTag, Comparable>;
  using SharedResultOp = co::Shared<co::Co<SharedContentResult>>;
  struct Stats final {
    uint64_t find_calls { 0 };
    uint64_t find_hits { 0 };
    uint64_t insert_calls { 0 };
    uint64_t erase_calls { 0 };
    uint64_t clear_calls { 0 };
    std::size_t active_type_buckets { 0 };
    std::size_t active_operations { 0 };
  };
  //! The identity an operation owns: the content type and its cache key.
  struct OperationKey final {
    TypeId type_id {};
    uint64_t cache_key { 0U };
  };
  struct RequestMeta final {
    LoadPriority priority { LoadPriority::kDefault };
    LoadIntent intent { LoadIntent::kRuntime };
    uint64_t sequence { 0 };
  };

  auto Open() noexcept -> void { accepting_ = true; }
  auto Close() -> void
  {
    accepting_ = false;
    Clear();
  }
  [[nodiscard]] auto NewOperationId() -> OperationId
  {
    if (!accepting_) {
      throw OperationCancelledException("Asset loader is stopped");
    }
    if (next_operation_id_ == 0U) {
      throw std::overflow_error("In-flight operation identities exhausted");
    }
    return OperationId { next_operation_id_++ };
  }

  auto Clear() -> void
  {
    ++stats_.clear_calls;
    // Coroutine destruction can re-enter Erase through its cleanup guard.
    while (!table_.empty()) {
      [[maybe_unused]] auto retired = table_.extract(table_.begin());
    }
    retired_.UnParkAll();
    idle_.UnParkAll();
  }
  auto Find(const OperationKey& key, const RequestMeta& request)
    -> std::optional<SharedResultOp>
  {
    ++stats_.find_calls;
    auto type_it = table_.find(key.type_id);
    if (type_it == table_.end()) {
      return std::nullopt;
    }
    auto it = type_it->second.find(key.cache_key);
    if (it == type_it->second.end()) {
      return std::nullopt;
    }
    if (it->second.op.Closed()) {
      throw std::logic_error(
        "A cancelled in-flight operation cannot be joined; await "
        "AwaitClosedOperation first");
    }
    ++stats_.find_hits;
    it->second.request = MergeRequestMeta(it->second.request, request);
    return it->second.op;
  }

  //! Wait while an operation whose waiters all left still owns the key.
  /*!
   The operation keeps running (see `RunToCompletion`) and may still publish,
   but it cannot be joined: `co::Shared` refuses parents that arrive after
   cancellation. It leaves the table when it ends.

   @return True when it waited; the caller looks the key up again.
  */
  auto AwaitClosedOperation(const OperationKey key) -> co::Co<bool>
  {
    auto waited = false;
    while (HasClosedOperation(key)) {
      waited = true;
      co_await retired_.Park();
    }
    co_return waited;
  }

  [[nodiscard]] auto GetRequestMeta(const OperationKey& key) const
    -> std::optional<RequestMeta>
  {
    const auto type_it = table_.find(key.type_id);
    if (type_it == table_.end()) {
      return std::nullopt;
    }
    const auto it = type_it->second.find(key.cache_key);
    if (it == type_it->second.end()) {
      return std::nullopt;
    }
    return it->second.request;
  }
  auto Insert(const OperationKey& key, OperationId id, SharedResultOp op,
    const RequestMeta& request) -> void
  {
    if (!accepting_) {
      throw OperationCancelledException("Asset loader is stopped");
    }
    const auto [bucket, created] = table_.try_emplace(key.type_id);
    try {
      const auto inserted
        = bucket->second
            .try_emplace(key.cache_key,
              Entry { .op = std::move(op), .request = request, .id = id })
            .second;
      if (!inserted) {
        throw std::logic_error("An in-flight operation already owns this key");
      }
    } catch (...) {
      if (created && bucket->second.empty()) {
        table_.erase(bucket);
      }
      throw;
    }
    ++stats_.insert_calls;
  }
  auto Erase(const OperationKey& key, OperationId id) -> void
  {
    ++stats_.erase_calls;
    const auto type_it = table_.find(key.type_id);
    if (type_it == table_.end()) {
      return;
    }
    const auto entry = type_it->second.find(key.cache_key);
    if (entry == type_it->second.end() || entry->second.id != id) {
      return;
    }
    {
      [[maybe_unused]] auto retired = type_it->second.extract(entry);
      if (type_it->second.empty()) {
        table_.erase(type_it);
      }
    }
    retired_.UnParkAll();
    if (table_.empty()) {
      idle_.UnParkAll();
    }
  }

  //! Wait for registered operations, including dependencies they register.
  auto WaitUntilEmpty() -> co::Co<>
  {
    while (!table_.empty()) {
      co_await idle_.Park();
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
    SharedResultOp op;
    RequestMeta request {};
    OperationId id { 0U };
  };

  [[nodiscard]] auto HasClosedOperation(const OperationKey& key) const -> bool
  {
    const auto type_it = table_.find(key.type_id);
    if (type_it == table_.end()) {
      return false;
    }
    const auto it = type_it->second.find(key.cache_key);
    return it != type_it->second.end() && it->second.op.Closed();
  }

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
  uint64_t next_operation_id_ { 1U };
  bool accepting_ { true };
  Stats stats_ {};
  co::ParkingLot idle_;
  co::ParkingLot retired_;
};

} // namespace oxygen::content::internal
