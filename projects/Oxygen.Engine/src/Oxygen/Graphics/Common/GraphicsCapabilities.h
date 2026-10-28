//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

//! Where the GPU's memory lives relative to system memory.
enum class MemoryArchitecture : std::uint8_t {
  //! The GPU has its own memory, separate from system memory.
  kDedicated,
  //! The GPU uses system memory; CPU and GPU caches are not coherent.
  kUnified,
  //! The GPU uses system memory with coherent CPU and GPU caches.
  kCacheCoherentUnified,
};

OXGN_GFX_NDAPI auto to_string(MemoryArchitecture value) -> const char*;

//! Device facts renderer policy reads. They are fixed for the lifetime of the
//! backend, because the device never changes while it runs.
struct GraphicsCapabilities {
  MemoryArchitecture memory_architecture { MemoryArchitecture::kDedicated };
};

} // namespace oxygen::graphics
