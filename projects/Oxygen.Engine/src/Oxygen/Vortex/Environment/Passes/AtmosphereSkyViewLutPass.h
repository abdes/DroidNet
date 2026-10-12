//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Registration.h>
#include <Oxygen/Vortex/Environment/Passes/Generated.AtmosphereContracts.h>
#include <Oxygen/Vortex/Types/EnvironmentViewData.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Texture;
}

namespace oxygen::graphics {
class CommandRecorder;
}

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
namespace internal {
  class RetainedTexturePool;
}
namespace testing {
  struct RendererPublicationProbe;
}

namespace environment::internal {
  class AtmosphereLutCache;
  struct StableAtmosphereState;
}

namespace environment {

  class AtmosphereSkyViewLutPass {
  public:
    struct RecordState {
      bool requested { false };
      bool executed { false };
      //! Submitted radiance resource retained for this view's consumers.
      std::shared_ptr<const graphics::Texture> texture;
      ShaderVisibleIndex sky_view_lut_srv { kInvalidShaderVisibleIndex };
      ShaderVisibleIndex sky_view_lut_uav { kInvalidShaderVisibleIndex };
      std::uint32_t width { 0U };
      std::uint32_t height { 0U };
      std::uint32_t dispatch_count_x { 0U };
      std::uint32_t dispatch_count_y { 0U };
      std::uint32_t dispatch_count_z { 0U };
    };

    OXGN_VRTX_API explicit AtmosphereSkyViewLutPass(Renderer& renderer);
    OXGN_VRTX_API ~AtmosphereSkyViewLutPass();

    AtmosphereSkyViewLutPass(const AtmosphereSkyViewLutPass&) = delete;
    auto operator=(const AtmosphereSkyViewLutPass&)
      -> AtmosphereSkyViewLutPass& = delete;
    AtmosphereSkyViewLutPass(AtmosphereSkyViewLutPass&&) = delete;
    auto operator=(AtmosphereSkyViewLutPass&&)
      -> AtmosphereSkyViewLutPass& = delete;

    OXGN_VRTX_API auto OnFrameStart(
      frame::SequenceNumber sequence, frame::Slot slot) -> void;
    OXGN_VRTX_API auto RemoveViewState(ViewId view_id) -> void;
    [[nodiscard]] OXGN_VRTX_API auto Record(RenderContext& ctx,
      graphics::CommandRecorder& recorder, const EnvironmentViewData& view_data,
      const internal::StableAtmosphereState& stable_state,
      const internal::AtmosphereLutCache& cache) -> RecordState;

    //! Writes a scene-linear LUT to caller-owned managed storage at the
    //! supplied capture referential, independent of the current camera/view.
    [[nodiscard]] OXGN_VRTX_API auto RecordCapture(RenderContext& ctx,
      graphics::CommandRecorder& recorder, const EnvironmentViewData& view_data,
      const internal::StableAtmosphereState& stable_state,
      const internal::AtmosphereLutCache& cache,
      const std::shared_ptr<graphics::Texture>& target,
      const graphics::RegistrationLease& registration) -> RecordState;

  private:
    auto RecordToTarget(RenderContext& ctx, graphics::CommandRecorder& recorder,
      const EnvironmentViewData& view_data,
      const internal::StableAtmosphereState& stable_state,
      const internal::AtmosphereLutCache& cache,
      const std::shared_ptr<graphics::Texture>& texture,
      ShaderVisibleIndex sky_view_srv, ShaderVisibleIndex sky_view_uav,
      bool capture) -> RecordState;
    friend struct ::oxygen::vortex::testing::RendererPublicationProbe;
    using PassConstants = AtmosphereSkyViewLutPassConstants;

    Renderer& renderer_;
    std::shared_ptr<upload::TransientStructuredBuffer> pass_constants_buffer_;
    std::unique_ptr<::oxygen::vortex::internal::RetainedTexturePool>
      output_pool_;
    std::vector<std::shared_ptr<graphics::Texture>> live_textures_;
  };

} // namespace environment
} // namespace oxygen::vortex
