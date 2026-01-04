//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

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
struct ViewPort;
struct Color;
struct Scissors;
class GraphicsPipelineDesc;
} // namespace oxygen::graphics

namespace oxygen::engine {

struct RenderContext;
struct RenderItem;
struct DrawMetadata; // forward declaration for predicate signature

// PassMaskBit is provided by PassMask.h include above

//! Abstract base class for a modular, coroutine-based render pass.
/*!
 RenderPass encapsulates a single stage of the rendering pipeline, such as
 geometry, shading, or post-processing. It is designed for use with modern,
 explicit graphics APIs (D3D12, Vulkan) and supports asynchronous (coroutine)
 resource preparation and execution, enabling fine-grained scheduling,
 parallelism, and non-blocking GPU work.

 Key design points:
  - Passes are modular and composable, supporting Forward+, deferred, or
 custom pipelines.
  - Resource state transitions and barriers are explicit and handled in
    PrepareResources.
  - Execution is coroutine-based, allowing for async GPU work, resource
 uploads, and synchronization.
  - Viewport, scissors, and clear color are set independently for flexibility
    and API consistency.
  - Passes can be enabled/disabled at runtime for debugging or feature
 toggling.

 Best practices for Forward+ and modern rendering:
  - Use PrepareResources to declare and transition all resources needed by the
    pass (framebuffers, buffers, etc.).
  - Use Execute for the main rendering logic, including pipeline setup,
 resource binding, and draw/dispatch calls.
  - Keep passes focused and modular (e.g., geometry pass, light culling pass,
    shading pass).
  - Use coroutines to compose passes, enable async GPU waits, and maximize
    parallelism.
  - Explicitly manage resource states to avoid hazards and maximize
 performance.
*/
class RenderPass : public Composition, public Named {
public:
  OXGN_RNDR_API explicit RenderPass(std::string_view name);
  ~RenderPass() override = default;

  OXYGEN_DEFAULT_COPYABLE(RenderPass)
  OXYGEN_DEFAULT_MOVABLE(RenderPass)

  //! Prepare and transition all resources needed for this pass.
  /*!
   This coroutine should explicitly declare and transition all input/output
   resources (textures, buffers, framebuffers, etc.) to the correct states for
   this pass, using the provided CommandRecorder. This includes inserting
   resource barriers, preparing descriptor tables, and ensuring all
   dependencies are met before execution.

   In Forward+ and modern explicit APIs, this step is critical for correctness
   and performance.

   \param recorder The command recorder for issuing resource transitions and
          setup commands.

   \return Coroutine handle (co::Co<>)
  */
  OXGN_RNDR_NDAPI auto PrepareResources(const RenderContext& context,
    graphics::CommandRecorder& recorder) -> co::Co<>;

  //! Execute the main rendering logic for this pass.
  /*!
   This coroutine should perform all rendering commands for the pass,
   including pipeline setup, resource binding, draw/dispatch calls, and any
   per-pass logic. It is called after PrepareResources and assumes all
   resources are in the correct state.

   Use this method to implement the core of geometry, shading, or
   post-processing passes.

   \param recorder The command recorder for issuing rendering commands.

   \return Coroutine handle (co::Co<>)
  */
  OXGN_RNDR_NDAPI auto Execute(const RenderContext& context,
    graphics::CommandRecorder& recorder) -> co::Co<>;

  //! Get the name of this pass (from Named interface).
  OXGN_RNDR_NDAPI auto GetName() const noexcept -> std::string_view override;

  //! Set the name of this pass (from Named interface).
  OXGN_RNDR_API auto SetName(std::string_view name) noexcept -> void override;

protected:
  OXGN_RNDR_NDAPI auto Context() const -> const RenderContext&;
  auto LastBuiltPsoDesc() const -> const auto& { return last_built_pso_desc_; }

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

  virtual auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<>
    = 0;
  virtual auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> = 0;
  virtual auto ValidateConfig() -> void = 0;
  virtual auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc = 0;
  virtual auto NeedRebuildPipelineState() const -> bool = 0;

  OXGN_RNDR_NDAPI static auto BuildRootBindings()
    -> std::vector<graphics::RootBindingItem>;

  // Issue draw calls over a specific pass partition.
  // Iterates PreparedSceneFrame partitions and emits draws only within the
  // ranges whose pass_mask includes the requested bit. Logs emitted count.
  auto IssueDrawCallsOverPass(graphics::CommandRecorder& recorder,
    PassMaskBit pass_bit) const noexcept -> void;

private:
  auto BindDrawIndexConstant(
    graphics::CommandRecorder& recorder, DrawIndex draw_index) const -> void;

  auto BindPassConstantsIndexConstant(graphics::CommandRecorder& recorder,
    ShaderVisibleIndex pass_constants_index) const -> void;

  auto BindSceneConstantsBuffer(graphics::CommandRecorder& recorder) const
    -> void;
  auto BindIndicesBuffer(graphics::CommandRecorder& recorder) const -> void;

  //! Emit draws for a half-open [begin, end) range with robust error
  //! handling. Increments counters for emitted, skipped invalid, and errors.
  auto EmitDrawRange(graphics::CommandRecorder& recorder,
    const DrawMetadata* records, uint32_t begin, uint32_t end,
    uint32_t& emitted_count, uint32_t& skipped_invalid,
    uint32_t& draw_errors) const noexcept -> void;

  //! Current render context.
  const RenderContext* context_ { nullptr };

  // Track the last built pipeline state object (PSO) description and hash, so
  // we can properly manage their caching and retrieval.
  std::optional<graphics::GraphicsPipelineDesc> last_built_pso_desc_;

  ShaderVisibleIndex pass_constants_index_ { kInvalidShaderVisibleIndex };
};

} // namespace oxygen::engine
