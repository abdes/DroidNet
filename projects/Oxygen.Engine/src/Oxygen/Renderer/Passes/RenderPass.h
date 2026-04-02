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
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
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

 ### Architecture Notes

 - Passes are modular and composable, supporting Forward+, deferred, or
   custom pipelines.
 - Resource state transitions and barriers are explicit and declared in
   `PrepareResources`.
 - Execution is coroutine-based, enabling async GPU work and fine-grained
   synchronization.

 @see GraphicsRenderPass, ComputeRenderPass
*/
class RenderPass : public Composition, public Named {
public:
  //! Construct a render pass with the given name.
  OXGN_RNDR_API explicit RenderPass(std::string_view name);
  ~RenderPass() override = default;

  OXYGEN_DEFAULT_COPYABLE(RenderPass)
  OXYGEN_DEFAULT_MOVABLE(RenderPass)

  //! Prepare and transition all resources needed for this pass.
  OXGN_RNDR_NDAPI auto PrepareResources(const RenderContext& context,
    graphics::CommandRecorder& recorder) -> co::Co<>;

  //! Execute the main rendering logic for this pass.
  OXGN_RNDR_NDAPI auto Execute(const RenderContext& context,
    graphics::CommandRecorder& recorder) -> co::Co<>;

  //! Return the name of this pass.
  OXGN_RNDR_NDAPI auto GetName() const noexcept -> std::string_view override;

  //! Set the name of this pass.
  OXGN_RNDR_API auto SetName(std::string_view name) noexcept -> void override;

protected:
  //! Return the current render context; valid only during pass execution.
  OXGN_RNDR_NDAPI auto Context() const -> const RenderContext&;

  //! Build the canonical engine root bindings from the generated table.
  OXGN_RNDR_NDAPI static auto BuildRootBindings()
    -> std::vector<graphics::RootBindingItem>;

  //! Store the pass-level constants index for use during execution.
  /*!
   Binds `g_PassConstantsIndex` (DWORD1 at `b2, space0`) once per pass.
   Call this from `DoPrepareResources` before execution begins.

   @param pass_constants_index Shader-visible heap index of the per-pass
          constants buffer.
   @see GetPassConstantsIndex, SetPassConstantsIndex
  */
  auto SetPassConstantsIndex(ShaderVisibleIndex pass_constants_index) noexcept
    -> void
  {
    pass_constants_index_ = pass_constants_index;
  }

  //! Return the pass-level constants index set for this execution.
  /*!
   Returns `kInvalidShaderVisibleIndex` if `SetPassConstantsIndex` has not
   been called yet.

   @return Shader-visible heap index of the per-pass constants buffer.
   @see SetPassConstantsIndex
  */
  [[nodiscard]] auto GetPassConstantsIndex() const noexcept
    -> ShaderVisibleIndex
  {
    return pass_constants_index_;
  }

  //=== Hooks for Derived Base Classes ===-----------------------------------//

  //! Validate the pass configuration before resource preparation.
  virtual auto ValidateConfig() -> void = 0;

  //! Called during `PrepareResources` after `ValidateConfig`; handle PSO
  //! rebuild here.
  virtual auto OnPrepareResources(graphics::CommandRecorder& recorder) -> void
    = 0;

  //! Prepare pass-specific resources (barriers, descriptors, uploads).
  virtual auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<>
    = 0;

  //! Called during `Execute` before `DoExecute`; set pipeline state here.
  virtual auto OnExecute(graphics::CommandRecorder& recorder) -> void = 0;

  //! Issue pass-specific draw or dispatch commands.
  virtual auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> = 0;

  //=== Draw Helpers (for GraphicsRenderPass derivatives) ===-----------------//

  //! Bind the per-draw root constant for the given draw record index.
  OXGN_RNDR_API auto BindDrawIndexConstant(
    graphics::CommandRecorder& recorder, DrawIndex draw_index) const -> void;

  //! Emit draws for the half-open range `[begin, end)` with error isolation.
  OXGN_RNDR_API auto EmitDrawRange(graphics::CommandRecorder& recorder,
    const DrawMetadata* records, uint32_t begin, uint32_t end,
    uint32_t& emitted_count, uint32_t& skipped_invalid,
    uint32_t& draw_errors) const noexcept -> void;

  //! Issue draw calls for all partitions whose mask includes `pass_bit`.
  OXGN_RNDR_API auto IssueDrawCallsOverPass(graphics::CommandRecorder& recorder,
    PassMaskBit pass_bit) const noexcept -> void;

private:
  const RenderContext* context_ { nullptr };
  ShaderVisibleIndex pass_constants_index_ { kInvalidShaderVisibleIndex };
};

} // namespace oxygen::engine
