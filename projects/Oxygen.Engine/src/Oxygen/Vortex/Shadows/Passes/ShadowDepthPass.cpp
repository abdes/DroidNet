//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Shadows/Passes/ShadowDepthPass.h>

#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::vortex::shadows {

ShadowDepthPass::ShadowDepthPass(Renderer& renderer)
  : renderer_(renderer)
{
}

ShadowDepthPass::~ShadowDepthPass() = default;

auto ShadowDepthPass::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  current_sequence_ = sequence;
  current_slot_ = slot;
  last_render_state_ = {};
}

auto ShadowDepthPass::Record(const PreparedViewShadowInput& /*view_input*/,
  const DirectionalShadowFrameData& frame_data,
  const std::span<const DrawCommand> draw_commands) -> RenderState
{
  last_render_state_ = {
    .rendered_cascade_count = frame_data.bindings.cascade_count,
    .rendered_draw_count = static_cast<std::uint32_t>(draw_commands.size())
      * frame_data.bindings.cascade_count,
  };
  return last_render_state_;
}

} // namespace oxygen::vortex::shadows
