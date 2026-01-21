//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Forward.h>

namespace oxygen::engine::internal {

// Interface for providers that expose Image-Based Lighting outputs
// (diffuse irradiance and specular prefilter maps).
class IIblProvider {
public:
  virtual ~IIblProvider() = default;

  //! Ensures resources (textures, views) are created. Returns true on success.
  virtual auto EnsureResourcesCreated() -> bool = 0;

  //! Snapshot of IBL output slots and a monotonic generation token.
  struct OutputMaps {
    ShaderVisibleIndex irradiance { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex prefilter { kInvalidShaderVisibleIndex };
    std::uint32_t prefilter_mip_levels { 0U };
    std::uint64_t generation { 0ULL };
  };

  //! Query the provider for the outputs corresponding to a given source
  //! cubemap slot. The provider returns `kInvalidDescriptorSlot` in the
  //! slots while outputs are not ready and a monotonic `generation` that
  //! increases each time outputs are (re)generated.
  [[nodiscard]] virtual auto QueryOutputsFor(
    ShaderVisibleIndex source_slot) const noexcept -> OutputMaps
    = 0;
};

} // namespace oxygen::engine::internal
