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
#include <Oxygen/Renderer/Types/ClusterConfig.h>
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
 Specifies the cluster/tile configuration and required resource bindings for
 the Forward+ light culling pass.

 ### Default Behavior

 By default, the renderer uses **tile-based** Forward+ culling (2D grid with
 16×16 tiles). This is efficient for most scenes and requires no configuration.

 ### Scene-Level Override via Attachment

 To enable clustered (3D) culling for a specific scene, attach an override
 to the **scene root node** with domain `kRendering`:

 ```cpp
 // In scene loading or game code:
 OverrideAttachment cluster_override;
 cluster_override.domain = OverrideDomain::kRendering;
 cluster_override.inheritable = true;
 cluster_override.properties["rndr_cluster_mode"] = uint32_t{1};  // 1 =
 clustered cluster_override.properties["rndr_cluster_depth"] = uint32_t{24};
 scene.GetOverrideAttachments().Attach(scene.GetRootNode().Id(),
                                       std::move(cluster_override));

 // In Renderer (reads from scene root):
 ClusterConfig cfg = ClusterConfig::TileBased();  // Default
 if (auto* att = scene.GetOverrideAttachments().Get(
       scene.GetRootNode().Id(), OverrideDomain::kRendering)) {
   if (auto mode = att->Get<uint32_t>("rndr_cluster_mode"); mode && *mode == 1)
 { cfg = ClusterConfig::Clustered(); cfg.depth_slices =
 att->GetOr<uint32_t>("rndr_cluster_depth", 24);
   }
 }
 ```

 The renderer selects the appropriate baked shader permutation (`CLUSTERED=0`
 or `CLUSTERED=1`) based on the resolved configuration.

 @see ClusterConfig, EnvironmentDynamicData, override_slots.md
*/
struct LightCullingPassConfig {
  //! Cluster/tile configuration. Defaults to tile-based Forward+.
  ClusterConfig cluster { ClusterConfig::TileBased() };

  //! Optional name for debugging purposes.
  std::string debug_name { "LightCullingPass" };
};

//! Compute pass that performs Forward+ tile/clustered light culling.
/*!
 This pass dispatches a compute shader that culls positional lights (point and
 spot) against screen-space tiles or 3D clusters. The output is consumed by
 shading passes to evaluate only lights affecting each pixel.

 ### Pipeline Position

 ```text
 DepthPrePass → [LightCullingPass] → ShaderPass → TransparentPass
 ```

 The pass requires a valid depth buffer from DepthPrePass to compute per-tile
 depth bounds (for tile-based) or per-cluster membership (for clustered).

 ### Outputs

 The pass produces two GPU resources accessible via bindless indices:

 1. **Cluster Grid** (`GetClusterGridSrvIndex()`):
    - For tile-based: `uint2` per tile containing `(light_offset, light_count)`
    - For clustered: `uint2` per cluster (3D grid flattened)

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

 @see ClusterConfig, LightCullingPassConfig, ForwardDirectLighting.hlsli
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
   Use `EnvironmentDynamicData.bindless_cluster_grid_slot` to pass to shaders.
  */
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetClusterGridSrvIndex() const noexcept
    -> ShaderVisibleIndex;

  //! Shader-visible SRV index for the light index list buffer.
  /*!
   Contains packed light indices referenced by the cluster grid offsets.
   Use `EnvironmentDynamicData.bindless_cluster_index_list_slot` to pass to
   shaders.
  */
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetLightIndexListSrvIndex() const noexcept
    -> ShaderVisibleIndex;

  //! Get the current cluster configuration.
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetClusterConfig() const noexcept
    -> const ClusterConfig&;

  //! Get computed grid dimensions for the current frame.
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetGridDimensions() const noexcept
    -> ClusterConfig::GridDimensions;

  //! GPU virtual address of the EnvironmentDynamicData CBV.
  /*!
   Returns the address of the constant buffer containing cluster grid slots,
   dimensions, and Z-binning parameters. Bound to root signature slot b3.

   @return GPU virtual address, or 0 if buffer not yet created.
  */
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetEnvironmentCbvAddress() const noexcept
    -> uint64_t;

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
