//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Internal/IblProcessor.h>

#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Vortex/Environment/Passes/IblProbePass.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::vortex::environment::internal {

IblProcessor::IblProcessor(Renderer& renderer)
  : renderer_(renderer)
  , probe_pass_(std::make_unique<environment::IblProbePass>())
{
}

IblProcessor::~IblProcessor() = default;

auto IblProcessor::RefreshPersistentProbes(
  const EnvironmentProbeState& current_state,
  const bool environment_source_changed) const -> RefreshState
{
  static_cast<void>(renderer_);
  const auto refreshed
    = probe_pass_->Refresh(current_state, environment_source_changed);
  return {
    .requested = refreshed.requested,
    .refreshed = refreshed.refreshed,
    .probe_state = refreshed.probe_state,
  };
}

auto IblProcessor::RefreshStaticSkyLightProducts(
  const EnvironmentProbeState& current_state,
  const SkyLightEnvironmentModel& sky_light) const -> RefreshState
{
  auto source_cubemap = std::shared_ptr<data::TextureResource> {};
  if (sky_light.enabled && sky_light.source == kSkyLightSourceSpecifiedCubemap
    && sky_light.cubemap_resource.get() != 0U) {
    if (const auto asset_loader = renderer_.GetAssetLoader();
      asset_loader != nullptr) {
      source_cubemap = asset_loader->GetTexture(sky_light.cubemap_resource);
      if (source_cubemap == nullptr) {
        asset_loader->StartLoadTexture(sky_light.cubemap_resource,
          [](std::shared_ptr<data::TextureResource>) { });
        source_cubemap = asset_loader->GetTexture(sky_light.cubemap_resource);
      }
    }
  }

  const auto refreshed = probe_pass_->RefreshStaticSkyLight(
    current_state, sky_light, source_cubemap.get());
  return {
    .requested = refreshed.requested,
    .refreshed = refreshed.refreshed,
    .probe_state = refreshed.probe_state,
  };
}

} // namespace oxygen::vortex::environment::internal
