//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Shared.h>

namespace oxygen::content::internal {

class InFlightOperationTable final {
public:
  using SharedVoidOp = co::Shared<co::Co<std::shared_ptr<void>>>;

  auto Clear() -> void;
  auto Find(TypeId type_id, uint64_t hash_key) const
    -> std::optional<SharedVoidOp>;
  auto InsertOrAssign(TypeId type_id, uint64_t hash_key, SharedVoidOp op)
    -> void;
  auto Erase(TypeId type_id, uint64_t hash_key) -> void;

private:
  std::unordered_map<TypeId, std::unordered_map<uint64_t, SharedVoidOp>> table_;
};

} // namespace oxygen::content::internal
