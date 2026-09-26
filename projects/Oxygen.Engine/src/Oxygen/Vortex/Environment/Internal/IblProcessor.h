//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Vortex/Environment/Internal/IblGpuProcessor.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentProbeState.h>
#include <Oxygen/Vortex/Environment/Types/SkyLightEnvironmentModel.h>
#include <Oxygen/Vortex/Types/SkyLightRuntimeState.h>
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
    //! Retire expired scene caches; report invalidated active publication.
    OXGN_VRTX_API auto OnFrameStart(frame::SequenceNumber sequence) -> bool;

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
    [[nodiscard]] OXGN_VRTX_API auto GetCachedSceneCount() const -> std::size_t;
    [[nodiscard]] OXGN_VRTX_API auto GetActivePoolStats() const
      -> IblGpuProcessor::Stats;
    [[nodiscard]] OXGN_VRTX_API auto GetTimingSampleCount() const
      -> std::uint64_t;
    [[nodiscard]] OXGN_VRTX_API auto InspectState(
      std::uint64_t scene_lifetime) const -> SkyLightRuntimeState;
    [[nodiscard]] OXGN_VRTX_API auto AcquireCapture(
      const std::shared_ptr<const IblGpuProducts>& products)
      -> Result<IblCaptureLease, IblCaptureError>;

  private:
    auto RetireExpiredScenes() -> void;
    struct Cache;

    Renderer& renderer_;
    std::unique_ptr<Cache> cache_;
  };

} // namespace internal
} // namespace oxygen::vortex::environment
