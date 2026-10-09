//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Vortex/Environment/Internal/IblGpuProcessor.h>

namespace oxygen::vortex {
class Renderer;
struct RenderContext;
namespace environment::internal {
  struct StableAtmosphereState;

  //! Scene-global sky source: the atmosphere when one is authored, otherwise
  //! the Sky Sphere, with height fog over either. Existing LUT builders feed
  //! the IBL slot's frozen snapshot; working LUTs are reused only after that
  //! copy is submitted. A cubemap Sky Sphere is read from `sky_sphere_cube`,
  //! and `sky_sphere_scale` calibrates the Sky Sphere's radiance.
  class CapturedSkySource final {
  public:
    OXGN_VRTX_API explicit CapturedSkySource(Renderer& renderer);
    OXGN_VRTX_API ~CapturedSkySource();
    CapturedSkySource(const CapturedSkySource&) = delete;
    auto operator=(const CapturedSkySource&) -> CapturedSkySource& = delete;

    [[nodiscard]] OXGN_VRTX_API auto Process(RenderContext& ctx,
      const StableAtmosphereState& state, const GpuFogParams& fog,
      IblGpuProcessor& processor,
      const std::shared_ptr<const IblBrdfProduct>& brdf,
      const IblProcessSettings& settings, std::uint32_t revision,
      const IblCubeView& sky_sphere_cube = {}, float sky_sphere_scale = 1.0F)
      -> std::expected<std::shared_ptr<const IblGpuProducts>, IblProcessError>;
    [[nodiscard]] OXGN_VRTX_API auto Begin(RenderContext& ctx,
      const StableAtmosphereState& state, const GpuFogParams& fog,
      IblGpuProcessor& processor,
      const std::shared_ptr<const IblBrdfProduct>& brdf,
      const IblProcessSettings& settings, std::uint32_t revision,
      const IblCubeView& sky_sphere_cube = {}, float sky_sphere_scale = 1.0F)
      -> std::expected<std::shared_ptr<IblGpuJob>, IblProcessError>;

  private:
    auto Prepare(RenderContext& ctx, const StableAtmosphereState& state,
      const GpuFogParams& fog, const IblCubeView& sky_sphere_cube,
      float sky_sphere_scale) -> std::expected<IblSkySource, IblProcessError>;
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };
} // namespace environment::internal
} // namespace oxygen::vortex
