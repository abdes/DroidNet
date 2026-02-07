//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Renderer/Passes/RenderPass.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine {

//! Base class for render passes that use a compute pipeline.
/*!
 ComputeRenderPass extends RenderPass with compute-specific pipeline state
 management. Derived passes implement CreateComputePipelineDesc() to define
 their compute PSO and NeedRebuildPipelineState() to signal when the PSO should
 be rebuilt.

 The base class handles:
 - Compute pipeline state caching and rebuilding
 - Setting the compute pipeline before DoExecute()

 ### Derived Class Responsibilities

 Derived classes must implement:
 - `DoPrepareResources()` - Resource transitions and buffer allocation
 - `DoExecute()` - Dispatch calls
 - `ValidateConfig()` - Configuration validation
 - `CreateComputePipelineDesc()` - Return the compute pipeline description
 - `NeedRebuildPipelineState()` - Return true when PSO needs rebuild

 @see RenderPass, GraphicsRenderPass, LightCullingPass
*/
class ComputeRenderPass : public RenderPass {
public:
  OXGN_RNDR_API explicit ComputeRenderPass(std::string_view name);
  ~ComputeRenderPass() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ComputeRenderPass)
  OXYGEN_DEFAULT_MOVABLE(ComputeRenderPass)

protected:
  [[nodiscard]] auto LastBuiltPsoDesc() const -> const auto&
  {
    return last_built_pso_desc_;
  }

  //=== RenderPass Interface (implemented by ComputeRenderPass) ===----------//

  OXGN_RNDR_API auto OnPrepareResources(graphics::CommandRecorder& recorder)
    -> void override;
  OXGN_RNDR_API auto OnExecute(graphics::CommandRecorder& recorder)
    -> void override;

  //=== ComputeRenderPass Interface (to be implemented by derived) ===-------//

  //! Create the compute pipeline state description for this pass.
  /*!
   Called when NeedRebuildPipelineState() returns true. Derived classes must
   return a valid ComputePipelineDesc configured for their specific compute
   requirements.

   @return The compute pipeline description.
  */
  virtual auto CreatePipelineStateDesc() -> graphics::ComputePipelineDesc = 0;

  //! Check if the pipeline state needs to be rebuilt.
  /*!
   Called during PrepareResources to determine if CreateComputePipelineDesc()
   should be called.

   @return True if the PSO should be rebuilt.
  */
  virtual auto NeedRebuildPipelineState() const -> bool = 0;

private:
  std::optional<graphics::ComputePipelineDesc> last_built_pso_desc_;
};

} // namespace oxygen::engine
