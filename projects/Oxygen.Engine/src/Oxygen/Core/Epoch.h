//===----------------------------------------------------------------------===//
// Epoch - strongly typed engine-wide epoch counter
//===----------------------------------------------------------------------===//
#pragma once

#include <Oxygen/Base/NamedType.h>

namespace oxygen {

// Engine-global Epoch type. Monotonic counter used for logical progress
// markers (frame boundaries, fence completions, upload batches). This is
// intentionally a NamedType to prevent accidental mixing with frame indices
// or timestamps.
using Epoch = oxygen::NamedType<uint64_t, struct EpochTag,
  oxygen::Printable,
  oxygen::Comparable,
  oxygen::PreIncrementable,
  oxygen::PostIncrementable>;

} // namespace oxygen
