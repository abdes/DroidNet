//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::content::constants {

//! Reserved loader-owned source id for synthetic resource keys.
/*!
 Synthetic keys are used for buffer-provided cooked payloads where the bytes do
 not originate from a mounted content source.

 This value is part of the Content runtime ABI contract: it MUST NOT collide
 with any mounted source id.
*/
inline constexpr uint16_t kSyntheticSourceId = 0xFFFF;

//! Base value for loader-assigned source ids for loose cooked mounts.
/*!
 Loose cooked sources are assigned ids from a dedicated range so they do not
 collide with dense PAK indices (0..N).

 The exact numeric value is a runtime contract that must be kept consistent
 across all Content code.
*/
inline constexpr uint16_t kLooseCookedSourceIdBase = 0x8000;

} // namespace oxygen::content::constants
