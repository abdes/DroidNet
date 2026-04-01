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
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Passes/ComputeRenderPass.h>
#include <Oxygen/Renderer/Types/LightCullingConfig.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
class Texture;
class Buffer;
} // namespace oxygen::graphics

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::engine {

struct RenderContext;

//! Configuration for the light culling compute pass.
/*!
 Holds only pass-local metadata for the final clustered light-grid build.


 * The grid shape is engine-owned and fixed to the shipping implementation.
 * This
 config intentionally does not expose runtime tuning knobs for slice
 * count, cell
 size, or manual depth range.

 @see LightCullingConfig,
 * LightingFrameBindings
 */
struct LightCullingPassConfig {
  //! Optional name for debugging purposes.
  std::string debug_name { "LightCullingPass" };
};

//! Compute pass that performs Clustered Forward light culling.
/*!
 This pass dispatches a compute shader that culls positional lights (point and
 spot) against 3D clusters. The output is consumed by shading passes to evaluate
 only lights affecting each pixel.

 ### Pipeline Position

 ```text
 [LightCullingPass] → ShaderPass → TransparentPass
 ```

 The pass builds the
 clustered light grid analytically from the view frustum.
 It does not consume
 scene depth or scene HZB.

 ### Outputs

 The pass produces two GPU resources accessible via bindless indices:

 1. **Cluster Grid** (`GetClusterGridSrvIndex()`):
    - `uint2` per cluster (3D grid flattened) containing `(light_offset,
 light_count)`

 2. **Light Index List** (`GetLightIndexListSrvIndex()`):
    - `uint` array containing light indices packed contiguously per cluster

 ### Upload Services Pattern

 This pass accesses staging and transfer services
 via `RenderContext`:

 ```cpp
 // In PrepareResources() - lazy buffer creation:
 auto& staging = context.GetRenderer().GetStagingProvider();
 auto& transfers = context.GetRenderer().GetInlineTransfersCoordinator();
 ```

 This avoids constructor injection of internal Renderer components, allowing
 the pass to be instantiated by application render graphs.

 @note This pass does NOT cull directional lights (they affect all pixels).
       Directional lights are handled separately in the shading loop.

 @see LightCullingConfig, LightCullingPassConfig, ForwardDirectLighting.hlsli
*/
class LightCullingPass : public ComputeRenderPass {
public:
  using Config = LightCullingPassConfig;

  //! Constructor.
  /*!
   @param gfx Graphics system for resource creation.
   @param config Pass configuration.
  */
  OXGN_RNDR_API LightCullingPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);

  OXGN_RNDR_API ~LightCullingPass() override;

  OXYGEN_MAKE_NON_COPYABLE(LightCullingPass)
  OXYGEN_DEFAULT_MOVABLE(LightCullingPass)

  //=== Output Accessors ===--------------------------------------------------//

  //! Shader-visible SRV index for the cluster grid buffer.
  /*!
   The cluster grid contains `uint2(light_offset, light_count)` per
   * cluster.
   Publish it through
   * `LightingFrameBindings.light_culling.bindless_cluster_grid_slot`.
  */
  OXGN_RNDR_NDAPI auto GetClusterGridSrvIndex() const noexcept
    -> ShaderVisibleIndex;

  //! Shader-visible SRV index for the light index list buffer.
  /*!
   Contains packed light indices referenced by the cluster grid offsets.

   * Publish it through

   * `LightingFrameBindings.light_culling.bindless_cluster_index_list_slot`.
 */
  OXGN_RNDR_NDAPI auto GetLightIndexListSrvIndex() const noexcept
    -> ShaderVisibleIndex;

  //! Underlying cluster grid buffer consumed by downstream passes.
  OXGN_RNDR_NDAPI auto GetClusterGridBuffer() const noexcept
    -> std::shared_ptr<const graphics::Buffer>;

  //! Underlying light index list buffer consumed by downstream passes.
  OXGN_RNDR_NDAPI auto GetLightIndexListBuffer() const noexcept
    -> std::shared_ptr<const graphics::Buffer>;

  //! Get the current published light-grid configuration.
  OXGN_RNDR_NDAPI auto GetClusterConfig() const noexcept
    -> const LightCullingConfig&;

  //! Get computed grid dimensions for the current frame.
  OXGN_RNDR_NDAPI auto GetGridDimensions() const noexcept
    -> LightCullingConfig::GridDimensions;

  //! Build a human-readable telemetry dump for the current pass instance.
  OXGN_RNDR_NDAPI auto BuildTelemetryDump() const -> std::string;

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
