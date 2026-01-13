//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <utility>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Renderer/Types/EnvironmentStaticData.h>

namespace oxygen::engine::internal {

//! Interface for sky atmosphere LUT providers.
/*!
 Abstracts the LUT manager for dependency injection into
 EnvironmentStaticDataManager. Provides read-only access to LUT slots and
 dimensions.
*/
class ISkyAtmosphereLutProvider {
public:
  virtual ~ISkyAtmosphereLutProvider() = default;

  //! Update cached parameters and set dirty flag if changed.
  virtual auto UpdateParameters(const GpuSkyAtmosphereParams& params) -> void
    = 0;

  //! Returns shader-visible SRV index for the transmittance LUT.
  [[nodiscard]] virtual auto GetTransmittanceLutSlot() const noexcept
    -> ShaderVisibleIndex
    = 0;

  //! Returns shader-visible SRV index for the sky-view LUT.
  [[nodiscard]] virtual auto GetSkyViewLutSlot() const noexcept
    -> ShaderVisibleIndex
    = 0;

  //! Returns transmittance LUT dimensions.
  [[nodiscard]] virtual auto GetTransmittanceLutSize() const noexcept
    -> Extent<uint32_t>
    = 0;

  //! Returns sky-view LUT dimensions.
  [[nodiscard]] virtual auto GetSkyViewLutSize() const noexcept
    -> Extent<uint32_t>
    = 0;
};

} // namespace oxygen::engine::internal
