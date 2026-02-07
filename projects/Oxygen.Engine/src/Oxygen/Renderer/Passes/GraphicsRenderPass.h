//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"
#include <optional>
#include <string_view>

#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Renderer/Passes/RenderPass.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine {

//! Base class for render passes that use a graphics pipeline.
/*!
 GraphicsRenderPass extends RenderPass with graphics-specific pipeline state
 management. Derived passes implement CreatePipelineStateDesc() to define their
 graphics PSO and NeedRebuildPipelineState() to signal when the PSO should be
 rebuilt.

 The base class handles:
 - Pipeline state caching and rebuilding
 - Setting the graphics pipeline before DoExecute()
 - Binding indices buffer and scene constants
 - Pass constants index binding

 ### Derived Class Responsibilities

 Derived classes must implement:
 - `DoPrepareResources()` - Resource transitions and setup
 - `DoSetupPipeline()` - Additional per-partition PSO setup if needed
 - `DoExecute()` - Draw call emission
 - `ValidateConfig()` - Configuration validation
 - `CreatePipelineStateDesc()` - Return the graphics pipeline description
 - `NeedRebuildPipelineState()` - Return true when PSO needs rebuild

 @see RenderPass, ComputeRenderPass, DepthPrePass, ShaderPass, TransparentPass
*/
class GraphicsRenderPass : public RenderPass {
public:
  ~GraphicsRenderPass() override = default;

  OXYGEN_MAKE_NON_COPYABLE(GraphicsRenderPass)
  OXYGEN_DEFAULT_MOVABLE(GraphicsRenderPass)

protected:
  //! Construct with optional SceneConstants binding.
  OXGN_RNDR_API explicit GraphicsRenderPass(
    std::string_view name, bool require_scene_constants = true);

  [[nodiscard]] auto LastBuiltPsoDesc() const -> const auto&
  {
    return last_built_pso_desc_;
  }

  [[nodiscard]] auto RequiresSceneConstants() const noexcept -> bool
  {
    return require_scene_constants_;
  }

  //=== RenderPass Interface (implemented by GraphicsRenderPass) ===---------//

  OXGN_RNDR_API auto OnPrepareResources(graphics::CommandRecorder& recorder)
    -> void override;
  OXGN_RNDR_API auto OnExecute(graphics::CommandRecorder& recorder)
    -> void override;

  //=== GraphicsRenderPass Interface (to be implemented by derived) ===------//

  //! Create the graphics pipeline state description for this pass.
  /*!
   Called when NeedRebuildPipelineState() returns true. Derived classes must
   return a valid GraphicsPipelineDesc configured for their specific rendering
   requirements.

   @return The graphics pipeline description.
  */
  virtual auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc = 0;

  //! Check if the pipeline state needs to be rebuilt.
  /*!
   Called during PrepareResources to determine if CreatePipelineStateDesc()
   should be called. Typical triggers include viewport/framebuffer changes.

   @return True if the PSO should be rebuilt.
  */
  virtual auto NeedRebuildPipelineState() const -> bool = 0;

  //! Optional hook for per-partition or additional pipeline setup.
  /*!
   Called after the base pipeline is set but before DoExecute(). Override to
   set up partition-specific PSO variants or additional state.

   Default implementation does nothing.

   @param recorder The command recorder.
  */
  virtual OXGN_RNDR_API auto DoSetupPipeline(
    graphics::CommandRecorder& recorder) -> void;

  //=== Helper Methods ===---------------------------------------------------//

  auto RebindCommonRootParameters(graphics::CommandRecorder& recorder) const
    -> void;

private:
  auto BindPassConstantsIndexConstant(graphics::CommandRecorder& recorder,
    ShaderVisibleIndex pass_constants_index) const -> void;
  auto BindSceneConstantsBuffer(graphics::CommandRecorder& recorder) const
    -> void;
  auto BindIndicesBuffer(graphics::CommandRecorder& recorder) const -> void;

  std::optional<graphics::GraphicsPipelineDesc> last_built_pso_desc_;
  bool require_scene_constants_ { true };
};

} // namespace oxygen::engine
