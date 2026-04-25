//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ImGui/ImGuiGraphicsBackend.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/Passes/ImGuiOverlayPass.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::vortex {

namespace {

auto TrackTextureFromKnownOrInitial(graphics::CommandRecorder& recorder,
  const graphics::Texture& texture) -> void
{
  if (recorder.IsResourceTracked(texture)) {
    return;
  }
  if (recorder.AdoptKnownResourceState(texture)) {
    return;
  }

  const auto initial = texture.GetDescriptor().initial_state;
  CHECK_F(initial != graphics::ResourceStates::kUnknown
      && initial != graphics::ResourceStates::kUndefined,
    "ImGuiOverlayPass: cannot track '{}' without a known or declared initial "
    "state",
    texture.GetName());
  recorder.BeginTrackingResourceState(texture, initial);
}

} // namespace

ImGuiOverlayPass::ImGuiOverlayPass(Renderer& renderer)
  : renderer_(renderer)
{
}

ImGuiOverlayPass::~ImGuiOverlayPass() = default;

auto ImGuiOverlayPass::Record(const Inputs& inputs) const -> bool
{
  if (inputs.backend == nullptr || inputs.target == nullptr
    || inputs.color_texture == nullptr) {
    return false;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return false;
  }

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(queue_key, "Vortex ImGuiOverlay");
  if (!recorder) {
    return false;
  }

  TrackTextureFromKnownOrInitial(*recorder, *inputs.color_texture);
  recorder->RequireResourceState(
    *inputs.color_texture, graphics::ResourceStates::kRenderTarget);
  recorder->FlushBarriers();
  recorder->BindFrameBuffer(*inputs.target);
  recorder->ClearFramebuffer(*inputs.target);
  graphics::GpuEventScope scope(*recorder, "Vortex.ImGuiOverlay",
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass);
  inputs.backend->Render(*recorder);
  recorder->RequireResourceStateFinal(
    *inputs.color_texture, graphics::ResourceStates::kCommon);
  recorder->FlushBarriers();

  return true;
}

} // namespace oxygen::vortex
