//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassMeshProcessor.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h>

namespace oxygen::vortex {

BasePassModule::BasePassModule(
  Renderer& renderer, const SceneTexturesConfig& scene_textures_config)
  : renderer_(renderer)
  , mesh_processor_(std::make_unique<BasePassMeshProcessor>(renderer))
{
  static_cast<void>(scene_textures_config);
}

BasePassModule::~BasePassModule() = default;

void BasePassModule::Execute(RenderContext& ctx, SceneTextures& scene_textures)
{
  static_cast<void>(scene_textures);

  has_published_base_pass_products_ = false;
  has_completed_velocity_for_dynamic_geometry_ = false;
  if (config_.shading_mode != ShadingMode::kDeferred) {
    return;
  }

  const auto has_current_view_payload = mesh_processor_ != nullptr
    && ctx.current_view.prepared_frame != nullptr
    && ctx.current_view.prepared_frame->IsValid();
  if (!has_current_view_payload) {
    return;
  }

  const auto writes_velocity
    = config_.write_velocity && scene_textures.GetVelocity() != nullptr;
  mesh_processor_->BuildDrawCommands(
    *ctx.current_view.prepared_frame, config_.shading_mode, writes_velocity);
  has_completed_velocity_for_dynamic_geometry_
    = !config_.write_velocity || writes_velocity;

  static_cast<void>(renderer_);
  has_published_base_pass_products_ = true;
}

void BasePassModule::SetConfig(const BasePassConfig& config)
{
  config_ = config;
}

auto BasePassModule::HasPublishedBasePassProducts() const -> bool
{
  return has_published_base_pass_products_;
}

auto BasePassModule::HasCompletedVelocityForDynamicGeometry() const -> bool
{
  return has_completed_velocity_for_dynamic_geometry_;
}

} // namespace oxygen::vortex
