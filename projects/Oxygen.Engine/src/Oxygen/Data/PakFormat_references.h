//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::data::pak::core {

//! Locates one asset's resource bindings followed by its key references.
//! Offset is relative to the containing PAK or loose index; empty is all zero.
#pragma pack(push, 1)
struct AssetReferenceTable {
  uint64_t offset = 0;
  uint32_t resource_count = 0;
  uint32_t key_count = 0;
};
#pragma pack(pop)
static_assert(sizeof(AssetReferenceTable) == 16);

} // namespace oxygen::data::pak::core
