//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Vortex/Environment/Internal/LocalFogVolumeState.h>
#include <Oxygen/Vortex/Environment/Passes/Generated.FogContracts.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
}

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;

namespace environment {

  class LocalFogVolumeComposePass {
  public:
    struct RecordState {
      bool requested { false };
      bool executed { false };
      std::uint32_t draw_count { 0U };
      bool bound_scene_color { false };
      bool sampled_scene_depth { false };
      bool consumed_instance_buffer { false };
    };

    OXGN_VRTX_API explicit LocalFogVolumeComposePass(Renderer& renderer);
    OXGN_VRTX_API ~LocalFogVolumeComposePass();

    OXGN_VRTX_API auto OnFrameStart(
      frame::SequenceNumber sequence, frame::Slot slot) -> void;

    LocalFogVolumeComposePass(const LocalFogVolumeComposePass&) = delete;
    auto operator=(const LocalFogVolumeComposePass&)
      -> LocalFogVolumeComposePass& = delete;
    LocalFogVolumeComposePass(LocalFogVolumeComposePass&&) = delete;
    auto operator=(LocalFogVolumeComposePass&&)
      -> LocalFogVolumeComposePass& = delete;

    [[nodiscard]] OXGN_VRTX_API auto Record(RenderContext& ctx,
      graphics::CommandRecorder& recorder, const SceneTextures& scene_textures,
      const internal::LocalFogVolumeState::ViewProducts& products)
      -> RecordState;

  private:
    using PassConstants = LocalFogComposePassConstants;

    //! The pass constants buffer, created on first use; null without
    //! Graphics.
    auto EnsurePassConstantsBuffer()
      -> observer_ptr<upload::TransientStructuredBuffer>;

    Renderer& renderer_;
    std::optional<upload::TransientStructuredBuffer> pass_constants_buffer_;
  };

} // namespace environment
} // namespace oxygen::vortex
