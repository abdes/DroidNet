//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Internal/InFlightOperationTable.h>

namespace oxygen::content::internal {

auto InFlightOperationTable::Clear() -> void { table_.clear(); }

auto InFlightOperationTable::Find(const TypeId type_id,
  const uint64_t hash_key) const -> std::optional<SharedVoidOp>
{
  const auto type_it = table_.find(type_id);
  if (type_it == table_.end()) {
    return std::nullopt;
  }
  const auto it = type_it->second.find(hash_key);
  if (it == type_it->second.end()) {
    return std::nullopt;
  }
  return it->second;
}

auto InFlightOperationTable::InsertOrAssign(
  const TypeId type_id, const uint64_t hash_key, SharedVoidOp op) -> void
{
  table_[type_id].insert_or_assign(hash_key, std::move(op));
}

auto InFlightOperationTable::Erase(
  const TypeId type_id, const uint64_t hash_key) -> void
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

} // namespace oxygen::content::internal
