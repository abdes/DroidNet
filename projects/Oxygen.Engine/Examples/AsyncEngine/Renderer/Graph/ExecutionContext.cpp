//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ExecutionContext.h"

#include <Oxygen/Base/Logging.h>

#include "../../ModuleContext.h"
#include "../Integration/GraphicsLayerIntegration.h"

namespace oxygen::examples::asyncsim {

//! Enhanced CommandRecorder with AsyncEngine integration
class AsyncEngineCommandRecorder : public CommandRecorder {
public:
  explicit AsyncEngineCommandRecorder(TaskExecutionContext* context) noexcept
    : context_(context)
  {
  }

  auto SetViewport(const std::vector<float>& viewport) -> void override
  {
    if (context_ && context_->HasAsyncEngineIntegration()) {
      LOG_F(3, "[CommandRecorder] Setting viewport for frame {}",
        context_->GetFrameIndex());
    }

    // TODO: Implement actual viewport setting with graphics backend
    // when full graphics integration is complete
    (void)viewport;
  }

  auto ClearRenderTarget(ResourceHandle target, const std::vector<float>& color)
    -> void override
  {
    if (context_ && context_->HasAsyncEngineIntegration()) {
      LOG_F(3, "[CommandRecorder] Clearing render target {} for frame {}",
        target.get(), context_->GetFrameIndex());
    }

    // TODO: Implement actual render target clearing with graphics backend
    (void)target;
    (void)color;
  }

  auto DrawIndexedInstanced(uint32_t index_count, uint32_t instance_count,
    uint32_t start_index, int32_t base_vertex, uint32_t start_instance)
    -> void override
  {
    if (context_ && context_->HasAsyncEngineIntegration()) {
      LOG_F(3, "[CommandRecorder] DrawIndexedInstanced({}, {}) for frame {}",
        index_count, instance_count, context_->GetFrameIndex());
    }

    // TODO: Implement actual draw call recording with graphics backend
    (void)index_count;
    (void)instance_count;
    (void)start_index;
    (void)base_vertex;
    (void)start_instance;
  }

  [[nodiscard]] auto GetDebugInfo() const -> std::string override
  {
    if (!context_ || !context_->HasAsyncEngineIntegration()) {
      return "AsyncEngineCommandRecorder (no integration)";
    }

    return "AsyncEngineCommandRecorder - Frame: "
      + std::to_string(context_->GetFrameIndex())
      + ", Parallel: " + (context_->IsParallelSafe() ? "Yes" : "No");
  }

private:
  TaskExecutionContext* context_;
};

//! Factory function to create AsyncEngine-integrated command recorder
auto CreateAsyncEngineCommandRecorder(TaskExecutionContext* context)
  -> std::unique_ptr<CommandRecorder>
{
  return std::make_unique<AsyncEngineCommandRecorder>(context);
}

} // namespace oxygen::examples::asyncsim
