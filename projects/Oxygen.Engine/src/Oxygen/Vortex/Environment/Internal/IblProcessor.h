//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Vortex/Environment/Internal/IblGpuProcessor.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentProbeState.h>
#include <Oxygen/Vortex/Environment/Types/SkyLightEnvironmentModel.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {
class Renderer;
struct RenderContext;
namespace resources {
  class TextureBinder;
}
}

namespace oxygen::data {
class TextureResource;
}

namespace oxygen::vortex::environment {

class IblProbePass;

namespace internal {

  struct StableAtmosphereState;

  class IblProcessor {
  public:
    struct RefreshState {
      bool requested { false };
      bool refreshed { false };
      EnvironmentProbeState probe_state {};
    };

    OXGN_VRTX_API explicit IblProcessor(Renderer& renderer);
    OXGN_VRTX_API ~IblProcessor();

    IblProcessor(const IblProcessor&) = delete;
    auto operator=(const IblProcessor&) -> IblProcessor& = delete;
    IblProcessor(IblProcessor&&) = delete;
    auto operator=(IblProcessor&&) -> IblProcessor& = delete;

    [[nodiscard]] OXGN_VRTX_API auto RefreshPersistentProbes(
      const EnvironmentProbeState& current_state,
      bool environment_source_changed) -> RefreshState;
    [[nodiscard]] OXGN_VRTX_API auto RefreshSkyLightProducts(
      const EnvironmentProbeState& current, RenderContext& ctx,
      const StableAtmosphereState& stable, const GpuFogParams& fog,
      const std::shared_ptr<resources::TextureBinder>& binder) -> RefreshState;
    [[nodiscard]] OXGN_VRTX_API auto GetPublishedProducts() const
      -> std::shared_ptr<const IblGpuProducts>;

  private:
    struct Cache;

    Renderer& renderer_;
    std::unique_ptr<Cache> cache_;
  };

} // namespace internal
} // namespace oxygen::vortex::environment
