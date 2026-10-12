//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Vortex/Environment/Passes/Generated.AtmosphereContracts.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {
struct RenderContext;
class Renderer;

namespace environment::internal {
  class AtmosphereLutCache;
  struct StableAtmosphereState;
}

namespace environment {

  class AtmosphereMultiScatteringLutPass {
  public:
    struct RecordState {
      bool requested { false };
      bool executed { false };
      ShaderVisibleIndex multi_scattering_lut_srv {
        kInvalidShaderVisibleIndex
      };
      ShaderVisibleIndex multi_scattering_lut_uav {
        kInvalidShaderVisibleIndex
      };
      std::uint32_t width { 0U };
      std::uint32_t height { 0U };
      std::uint32_t dispatch_count_x { 0U };
      std::uint32_t dispatch_count_y { 0U };
      std::uint32_t dispatch_count_z { 0U };
    };

    OXGN_VRTX_API explicit AtmosphereMultiScatteringLutPass(Renderer& renderer);
    OXGN_VRTX_API ~AtmosphereMultiScatteringLutPass();

    AtmosphereMultiScatteringLutPass(const AtmosphereMultiScatteringLutPass&)
      = delete;
    auto operator=(const AtmosphereMultiScatteringLutPass&)
      -> AtmosphereMultiScatteringLutPass& = delete;
    AtmosphereMultiScatteringLutPass(AtmosphereMultiScatteringLutPass&&)
      = delete;
    auto operator=(AtmosphereMultiScatteringLutPass&&)
      -> AtmosphereMultiScatteringLutPass& = delete;

    OXGN_VRTX_API auto OnFrameStart(
      frame::SequenceNumber sequence, frame::Slot slot) -> void;
    [[nodiscard]] OXGN_VRTX_API auto Record(RenderContext& ctx,
      const internal::StableAtmosphereState& stable_state,
      internal::AtmosphereLutCache& cache, bool capture = false) -> RecordState;

  private:
    using PassConstants = AtmosphereMultiScatteringLutPassConstants;

    Renderer& renderer_;
    std::shared_ptr<upload::TransientStructuredBuffer> pass_constants_buffer_;
  };

} // namespace environment
} // namespace oxygen::vortex
