//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Passes/ComputeRenderPass.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
class Buffer;
} // namespace oxygen::graphics

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::engine::internal {
class SkyAtmosphereLutManager;
} // namespace oxygen::engine::internal

namespace oxygen::engine {

//! Configuration for the sky atmosphere LUT compute pass.
struct SkyAtmosphereLutComputePassConfig {
  //! Manager that owns the LUT textures and tracks dirty state.
  observer_ptr<internal::SkyAtmosphereLutManager> lut_manager { nullptr };

  //! Optional name for debugging purposes.
  std::string debug_name { "SkyAtmosphereLutComputePass" };
};

//! Compute pass that generates atmosphere precomputation LUTs.
/*!
 Dispatches compute shaders to generate the transmittance and sky-view LUTs
 used for physically-based atmospheric scattering. Only executes when the
 `SkyAtmosphereLutManager` reports that atmosphere parameters have changed.

 ### Pipeline Position

 ```text
 [SkyAtmosphereLutComputePass] → LightCullingPass → SkyPass → ...
 ```

 This pass runs early in the frame, before any passes that need atmosphere
 data for rendering. The generated LUTs are persistent and only regenerated
 when atmosphere parameters change.

 ### Generated LUTs

 1. **Transmittance LUT** (256×64, RGBA16F):
   - Precomputed **optical depth** integrals (per component)
   - RGB = optical depth for Rayleigh, Mie, absorption (ozone-like)
   - A = reserved (unused)

 2. **Sky-View LUT** (192×108, RGBA16F):
    - Inscattered radiance for all view directions
    - RGB = inscattered radiance, A = transmittance

 ### Execution Flow

 1. Check `lut_manager->IsDirty()` - skip if LUTs are up-to-date
 2. Dispatch transmittance LUT compute shader
 3. UAV barrier for transmittance LUT
 4. Dispatch sky-view LUT compute shader (reads transmittance)
 5. Call `lut_manager->MarkClean()`

 @see SkyAtmosphereLutManager, SkyPass, GpuSkyAtmosphereParams
*/
class SkyAtmosphereLutComputePass : public ComputeRenderPass {
public:
  using Config = SkyAtmosphereLutComputePassConfig;

  //! Constructor.
  /*!
   @param gfx Graphics system for resource creation.
   @param config Pass configuration including the LUT manager.
  */
  OXGN_RNDR_API SkyAtmosphereLutComputePass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);

  OXGN_RNDR_API ~SkyAtmosphereLutComputePass() override;

  OXYGEN_MAKE_NON_COPYABLE(SkyAtmosphereLutComputePass)
  OXYGEN_DEFAULT_MOVABLE(SkyAtmosphereLutComputePass)

protected:
  //=== ComputeRenderPass Interface
  //===-----------------------------------------//

  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;
  auto CreatePipelineStateDesc() -> graphics::ComputePipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::engine
