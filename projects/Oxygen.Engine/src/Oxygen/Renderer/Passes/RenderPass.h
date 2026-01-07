//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Types/DrawIndex.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen::engine {

struct RenderContext;
struct DrawMetadata;

//! Abstract base class for a modular, coroutine-based render pass.
/*!
 RenderPass encapsulates a single stage of the rendering pipeline, such as
 geometry, shading, compute, or post-processing. It is designed for use with
 modern, explicit graphics APIs (D3D12, Vulkan) and supports asynchronous
 (coroutine) resource preparation and execution, enabling fine-grained
 scheduling, parallelism, and non-blocking GPU work.

 ### Class Hierarchy

 RenderPass is a pure interface. Derived classes should inherit from the
 appropriate intermediate base class:

 - **GraphicsRenderPass**: For passes using graphics pipelines (vertex/pixel
   shaders, rasterization, draw calls). Examples: DepthPrePass, ShaderPass,
   TransparentPass.

 - **ComputeRenderPass**: For passes using compute pipelines (compute shaders,
   dispatch calls). Examples: LightCullingPass, SSAOPass.

 ### Key Design Points

 - Passes are modular and composable, supporting Forward+, deferred, or custom
   pipelines.
 - Resource state transitions and barriers are explicit and handled in
   PrepareResources.
 - Execution is coroutine-based, allowing for async GPU work, resource uploads,
   and synchronization.
 - Passes can be enabled/disabled at runtime for debugging or feature toggling.

 @see GraphicsRenderPass, ComputeRenderPass
*/
class RenderPass : public Composition, public Named {
public:
  OXGN_RNDR_API explicit RenderPass(std::string_view name);
  ~RenderPass() override = default;

  OXYGEN_DEFAULT_COPYABLE(RenderPass)
  OXYGEN_DEFAULT_MOVABLE(RenderPass)

  //! Prepare and transition all resources needed for this pass.
  /*!
   This coroutine explicitly declares and transitions all input/output resources
   (textures, buffers, framebuffers, etc.) to the correct states for this pass,
   using the provided CommandRecorder. This includes inserting resource
   barriers, preparing descriptor tables, and ensuring all dependencies are met
   before execution.

   @param context The render context containing shared frame data.
   @param recorder The command recorder for issuing resource transitions.
   @return Coroutine handle (co::Co<>)
  */
  OXGN_RNDR_NDAPI auto PrepareResources(const RenderContext& context,
    graphics::CommandRecorder& recorder) -> co::Co<>;

  //! Execute the main rendering logic for this pass.
  /*!
   This coroutine performs all rendering commands for the pass, including
   pipeline setup, resource binding, draw/dispatch calls, and any per-pass
   logic. It is called after PrepareResources and assumes all resources are in
   the correct state.

   @param context The render context containing shared frame data.
   @param recorder The command recorder for issuing rendering commands.
   @return Coroutine handle (co::Co<>)
  */
  OXGN_RNDR_NDAPI auto Execute(const RenderContext& context,
    graphics::CommandRecorder& recorder) -> co::Co<>;

  //! Get the name of this pass (from Named interface).
  OXGN_RNDR_NDAPI auto GetName() const noexcept -> std::string_view override;

  //! Set the name of this pass (from Named interface).
  OXGN_RNDR_API auto SetName(std::string_view name) noexcept -> void override;

protected:
  //! Access the current render context during pass execution.
  OXGN_RNDR_NDAPI auto Context() const -> const RenderContext&;

  //! Build the canonical engine root bindings from the generated table.
  /*!
   This produces root bindings that match the bindless engine root signature
   generated from Bindless.yaml. Both graphics and compute pipelines must use
   the same layout so that shader ABI requirements (e.g., SceneConstants at
   b1, RootConstants at b2) are satisfied.
  */
  OXGN_RNDR_NDAPI static auto BuildRootBindings()
    -> std::vector<graphics::RootBindingItem>;

  //! Set the pass-level RootConstants payload for this pass execution.
  /*!
   This binds the `g_PassConstantsIndex` root constant (DWORD1 at `b2, space0`)
   once per pass.
  */
  auto SetPassConstantsIndex(ShaderVisibleIndex pass_constants_index) noexcept
    -> void
  {
    pass_constants_index_ = pass_constants_index;
  }

  [[nodiscard]] auto GetPassConstantsIndex() const noexcept
    -> ShaderVisibleIndex
  {
    return pass_constants_index_;
  }

  //=== Pure Virtual Interface ===-------------------------------------------//

  //! Validate the pass configuration.
  /*!
   Called during PrepareResources before any resource operations. Derived
   classes should throw std::runtime_error if configuration is invalid.
  */
  virtual auto ValidateConfig() -> void = 0;

  //! Prepare pass-specific resources.
  /*!
   Called after ValidateConfig. Derived classes should allocate buffers,
   transition resources, and prepare for execution.

   @param recorder The command recorder for resource operations.
  */
  virtual auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<>
    = 0;

  //! Execute pass-specific rendering logic.
  /*!
   Called after pipeline is set. Derived classes should issue draw or dispatch
   calls.

   @param recorder The command recorder for rendering commands.
  */
  virtual auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> = 0;

  //=== Hooks for Derived Base Classes ===-----------------------------------//

  //! Called during PrepareResources after ValidateConfig.
  /*!
   Override in GraphicsRenderPass/ComputeRenderPass to handle PSO rebuild.
  */
  virtual auto OnPrepareResources(graphics::CommandRecorder& recorder) -> void
    = 0;

  //! Called during Execute before DoExecute.
  /*!
   Override in GraphicsRenderPass/ComputeRenderPass to set pipeline state.
  */
  virtual auto OnExecute(graphics::CommandRecorder& recorder) -> void = 0;

  //=== Draw Helpers (for GraphicsRenderPass derivatives) ===-----------------//

  //! Bind the per-draw root constant identifying the draw record.
  /*!
   Derived passes that need to switch pipeline state per partition can use this
   helper while iterating ranges directly.

   @param recorder Command recorder used to bind the constant.
   @param draw_index Index into PreparedSceneFrame draw metadata.
  */
  OXGN_RNDR_API auto BindDrawIndexConstant(
    graphics::CommandRecorder& recorder, DrawIndex draw_index) const -> void;

  //! Emit draws for a half-open [begin, end) range with robust error handling.
  /*!
   Increments counters for emitted, skipped invalid, and errors.
  */
  OXGN_RNDR_API auto EmitDrawRange(graphics::CommandRecorder& recorder,
    const DrawMetadata* records, uint32_t begin, uint32_t end,
    uint32_t& emitted_count, uint32_t& skipped_invalid,
    uint32_t& draw_errors) const noexcept -> void;

  //! Issue draw calls over a specific pass partition.
  /*!
   Iterates PreparedSceneFrame partitions and emits draws only within the
   ranges whose pass_mask includes the requested bit. Logs emitted count.
  */
  OXGN_RNDR_API auto IssueDrawCallsOverPass(graphics::CommandRecorder& recorder,
    PassMaskBit pass_bit) const noexcept -> void;

private:
  const RenderContext* context_ { nullptr };
  ShaderVisibleIndex pass_constants_index_ { kInvalidShaderVisibleIndex };
};

} // namespace oxygen::engine
