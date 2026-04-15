//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.h>

namespace oxygen::vortex {

DepthPrepassModule::DepthPrepassModule(Renderer& renderer)
  : renderer_(renderer)
{
}

DepthPrepassModule::~DepthPrepassModule() = default;

void DepthPrepassModule::Execute(
  RenderContext& ctx, SceneTextures& scene_textures)
{
  (void)renderer_;
  if (config_.mode == DepthPrePassMode::kDisabled) {
    completeness_ = DepthPrePassCompleteness::kDisabled;
    has_published_depth_products_ = false;
    return;
  }

  const auto has_current_view_payload = ctx.current_view.prepared_frame != nullptr;
  const auto can_write_velocity
    = !config_.write_velocity || scene_textures.GetVelocity() != nullptr;
  (void)has_current_view_payload;
  (void)can_write_velocity;

  // The shell integration only wires the stage boundary in 03-05. Real draw
  // processing and proof land in 03-06, so completeness remains intentionally
  // incomplete while the stage is enabled.
  completeness_ = DepthPrePassCompleteness::kIncomplete;
  has_published_depth_products_ = false;
}

void DepthPrepassModule::SetConfig(const DepthPrepassConfig& config)
{
  config_ = config;
}

auto DepthPrepassModule::GetCompleteness() const -> DepthPrePassCompleteness
{
  return completeness_;
}

auto DepthPrepassModule::HasPublishedDepthProducts() const -> bool
{
  return has_published_depth_products_;
}

} // namespace oxygen::vortex
