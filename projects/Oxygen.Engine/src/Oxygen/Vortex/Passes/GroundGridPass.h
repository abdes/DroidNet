//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <unordered_map>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Buffer;
class Framebuffer;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;

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

  [[nodiscard]] OXGN_VRTX_API auto Record(
    RenderContext& ctx, const SceneTextures& scene_textures,
    observer_ptr<const graphics::Framebuffer> target = {})
    -> RecordState;

private:
  struct PassConstants;
  struct SmoothState {
    glm::dvec2 grid_offset { 0.0, 0.0 };
    glm::dvec2 velocity { 0.0, 0.0 };
    bool first_frame { true };
  };

  auto EnsurePassConstantsBuffer() -> void;
  auto ReleasePassConstantsBuffer() -> void;
  auto UpdatePassConstants(const RenderContext& ctx) -> ShaderVisibleIndex;
  [[nodiscard]] auto ComputeInvViewProj(const RenderContext& ctx) const
    -> glm::mat4;
  auto ComputeGridOffset(PassConstants& constants, const RenderContext& ctx)
    -> void;
  auto FillConstants(PassConstants& constants) const -> void;

  Renderer& renderer_;
  std::shared_ptr<graphics::Buffer> pass_constants_buffer_ {};
  std::byte* pass_constants_mapped_ptr_ { nullptr };
  std::array<ShaderVisibleIndex, 8U> pass_constants_indices_ {};
  std::size_t pass_constants_slot_ { 0U };
  std::unordered_map<ViewId, SmoothState> smooth_states_by_view_;
};

} // namespace oxygen::vortex
