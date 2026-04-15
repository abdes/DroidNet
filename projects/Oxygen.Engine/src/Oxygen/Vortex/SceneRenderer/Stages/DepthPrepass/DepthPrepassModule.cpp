//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.h>

namespace oxygen::vortex {

DepthPrepassModule::DepthPrepassModule(
  Renderer& renderer, const SceneTexturesConfig& scene_textures_config)
  : renderer_(renderer)
  , mesh_processor_(std::make_unique<DepthPrepassMeshProcessor>(renderer))
{
  static_cast<void>(scene_textures_config);
}

DepthPrepassModule::~DepthPrepassModule() = default;

void DepthPrepassModule::Execute(
  RenderContext& ctx, SceneTextures& scene_textures)
{
  if (config_.mode == DepthPrePassMode::kDisabled) {
    completeness_ = DepthPrePassCompleteness::kDisabled;
    has_published_depth_products_ = false;
    return;
  }

  const auto has_current_view_payload = ctx.current_view.prepared_frame != nullptr
    && ctx.current_view.prepared_frame->IsValid();
  const auto can_write_velocity
    = !config_.write_velocity || scene_textures.GetVelocity() != nullptr;
  if (!has_current_view_payload || !can_write_velocity) {
    completeness_ = DepthPrePassCompleteness::kIncomplete;
    has_published_depth_products_ = false;
    return;
  }

  if (mesh_processor_ != nullptr) {
    mesh_processor_->BuildDrawCommands(
      *ctx.current_view.prepared_frame, config_.mode == DepthPrePassMode::kOpaqueAndMasked);
  }

  // Phase 03-06 locks the Stage 3 publication contract onto the prepared-frame
  // handoff even before the GPU command submission path is fully materialized.
  completeness_ = DepthPrePassCompleteness::kComplete;
  has_published_depth_products_ = true;
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
