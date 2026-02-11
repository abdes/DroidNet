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
 Specifies the cluster configuration and required resource bindings for
 the Clustered Forward light culling pass.

 ### Default Behavior

 By default, the renderer uses **clustered** Forward light culling (3D grid).
 16×16 tiles with 24 depth slices.

 ### Scene-Level Override via Attachment

 To override clustered culling parameters for a specific scene, attach an
 override to the **scene root node** with domain `kRendering`:

 ```cpp
 // In scene loading or game code:
 OverrideAttachment cluster_override;
 cluster_override.domain = OverrideDomain::kRendering;
 cluster_override.inheritable = true;
 cluster_override.properties["rndr_cluster_depth"] = uint32_t{32};
  scene.GetOverrideAttachments().Attach(scene.GetRootNode().Id(),
                                        std::move(cluster_override));

  // In Renderer (reads from scene root):
  LightCullingConfig cfg = LightCullingConfig::Default();
  if (auto* att = scene.GetOverrideAttachments().Get(
        scene.GetRootNode().Id(), OverrideDomain::kRendering)) {
    cfg.cluster_dim_z = att->GetOr<uint32_t>("rndr_cluster_depth", 24);
  }
  ```

  @see LightCullingConfig, EnvironmentDynamicData, override_slots.md
 */
struct LightCullingPassConfig {
  //! Cluster configuration. Defaults to Clustered Forward.
  LightCullingConfig cluster { LightCullingConfig::Default() };

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
 DepthPrePass → [LightCullingPass] → ShaderPass → TransparentPass
 ```

 The pass requires a valid depth buffer from DepthPrePass to compute
 per-cluster membership.

 ### Outputs

 The pass produces two GPU resources accessible via bindless indices:

 1. **Cluster Grid** (`GetClusterGridSrvIndex()`):
    - `uint2` per cluster (3D grid flattened) containing `(light_offset,
 light_count)`

 2. **Light Index List** (`GetLightIndexListSrvIndex()`):
    - `uint` array containing light indices packed contiguously per cluster

 ### Configuration via Override Attachments

 The cluster configuration can be overridden per-scene using the rendering
 domain attachment system. See `override_slots.md` for the complete design.

 ### Upload Services Pattern

 This pass accesses staging and transfer services via `RenderContext`:

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
   @param config Pass configuration including cluster settings.
  */
  OXGN_RNDR_API LightCullingPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);

  OXGN_RNDR_API ~LightCullingPass() override;

  OXYGEN_MAKE_NON_COPYABLE(LightCullingPass)
  OXYGEN_DEFAULT_MOVABLE(LightCullingPass)

  //=== Output Accessors ===--------------------------------------------------//

  //! Shader-visible SRV index for the cluster grid buffer.
  /*!
   The cluster grid contains `uint2(light_offset, light_count)` per cluster.
   Use `EnvironmentDynamicData.light_culling.bindless_cluster_grid_slot` to pass
   to shaders.
  */
  OXGN_RNDR_NDAPI auto GetClusterGridSrvIndex() const noexcept
    -> ShaderVisibleIndex;

  //! Shader-visible SRV index for the light index list buffer.
  /*!
   Contains packed light indices referenced by the cluster grid offsets.
   Use `EnvironmentDynamicData.light_culling.bindless_cluster_index_list_slot`
   to pass to shaders.
  */
  OXGN_RNDR_NDAPI auto GetLightIndexListSrvIndex() const noexcept
    -> ShaderVisibleIndex;

  //! Get the current cluster configuration.
  OXGN_RNDR_NDAPI auto GetClusterConfig() const noexcept
    -> const LightCullingConfig&;

  //! Get computed grid dimensions for the current frame.
  OXGN_RNDR_NDAPI auto GetGridDimensions() const noexcept
    -> LightCullingConfig::GridDimensions;

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
