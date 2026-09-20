//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <unordered_map>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Buffer;
class Framebuffer;
} // namespace oxygen::graphics

namespace oxygen::graphics {
class CommandRecorder;
}

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;
namespace testing {
  struct RendererPublicationProbe;
}
namespace internal {
  template <typename Payload> class PerViewStructuredPublisher;
}

class GroundGridPass {
public:
  struct RecordState {
    bool requested { false };
    bool executed { false };
    bool wrote_scene_color { false };
    bool sampled_scene_depth { false };
    std::uint32_t draw_count { 0U };
  };

  OXGN_VRTX_API explicit GroundGridPass(Renderer& renderer);
  OXGN_VRTX_API ~GroundGridPass();

  GroundGridPass(const GroundGridPass&) = delete;
  auto operator=(const GroundGridPass&) -> GroundGridPass& = delete;
  GroundGridPass(GroundGridPass&&) = delete;
  auto operator=(GroundGridPass&&) -> GroundGridPass& = delete;

  [[nodiscard]] OXGN_VRTX_API auto Record(RenderContext& ctx,
    graphics::CommandRecorder& recorder, const SceneTextures& scene_textures,
    observer_ptr<const graphics::Framebuffer> target = {}) -> RecordState;

private:
  friend struct testing::RendererPublicationProbe;
  struct PassConstants;
  struct SmoothState {
    glm::dvec2 grid_offset { 0.0, 0.0 };
    glm::dvec2 velocity { 0.0, 0.0 };
    bool first_frame { true };
  };

  OXGN_VRTX_API auto UpdatePassConstants(const RenderContext& ctx)
    -> ShaderVisibleIndex;
  [[nodiscard]] auto ComputeInvViewProj(const RenderContext& ctx) const
    -> glm::mat4;
  auto ComputeGridOffset(PassConstants& constants, const RenderContext& ctx)
    -> void;
  auto FillConstants(PassConstants& constants) const -> void;

  Renderer& renderer_;
  std::unique_ptr<internal::PerViewStructuredPublisher<PassConstants>>
    constants_publisher_;
  std::optional<frame::SequenceNumber> constants_frame_;
  std::unordered_map<ViewId, SmoothState> smooth_states_by_view_;
};

} // namespace oxygen::vortex
