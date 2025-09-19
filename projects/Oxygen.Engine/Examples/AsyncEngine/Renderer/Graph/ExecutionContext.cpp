//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ExecutionContext.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

#include <Oxygen/Base/Logging.h>

#include "../Integration/GraphicsLayerIntegration.h"
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/View.h>

// Hash function for pair<uint32_t, uint32_t>
namespace std {
template <> struct hash<std::pair<uint32_t, uint32_t>> {
  auto operator()(const std::pair<uint32_t, uint32_t>& p) const -> std::size_t
  {
    return std::hash<uint32_t> {}(p.first)
      ^ (std::hash<uint32_t> {}(p.second) << 1);
  }
};
}

namespace oxygen::engine::asyncsim {

//! Enhanced CommandRecorder with AsyncEngine integration and backend
//! abstraction
class AsyncEngineCommandRecorder : public CommandRecorder {
public:
  explicit AsyncEngineCommandRecorder(TaskExecutionContext* context) noexcept
    : context_(context)
  {
    LOG_F(3, "[CommandRecorder] Created with AsyncEngine integration: {}",
      context_ && context_->HasAsyncEngineIntegration());
  }

  auto SetViewport(const std::vector<float>& viewport) -> void override
  {
    if (context_ && context_->HasAsyncEngineIntegration()) {
      const auto& view_ctx = context_->GetViewInfo();
      LOG_F(3, "[CommandRecorder] Setting viewport for view '{}' frame {}",
        view_ctx.view_name, context_->GetFrameIndex());

      // Validate viewport dimensions match view context
      if (viewport.size() >= 4) {
        const auto width = static_cast<uint32_t>(viewport[2]);
        const auto height = static_cast<uint32_t>(viewport[3]);

        if (width != view_ctx.view->Viewport().width
          || height != view_ctx.view->Viewport().height) {
          LOG_F(WARNING,
            "[CommandRecorder] Viewport size mismatch: expected {}x{}, got "
            "{}x{}",
            view_ctx.view->Viewport().width, view_ctx.view->Viewport().height,
            width, height);
        }
      }
    }

    // Store viewport for potential graphics backend integration
    current_viewport_ = viewport;

    // TODO: Route to actual graphics command list when full integration is
    // complete
    LOG_F(9,
      "[CommandRecorder] Viewport set - integration needed for actual GPU "
      "commands");
  }

  auto SetPipelineState(void* pso) -> void override
  {
    if (context_ && context_->HasAsyncEngineIntegration()) {
      LOG_F(9, "[CommandRecorder] Setting pipeline state for frame {}",
        context_->GetFrameIndex());
    }

    current_pso_ = pso;

    // TODO: Implement actual PSO binding through graphics backend
    LOG_F(
      9, "[CommandRecorder] Pipeline state set - backend integration pending");
  }

  auto ClearRenderTarget(ResourceHandle target, const std::vector<float>& color)
    -> void override
  {
    if (context_ && context_->HasAsyncEngineIntegration()) {
      const auto& view_ctx = context_->GetViewInfo();
      LOG_F(3,
        "[CommandRecorder] Clearing render target {} for view '{}' frame {}",
        target.get(), view_ctx.view_name, context_->GetFrameIndex());

      // Track resource usage for render graph validation
      context_->AddWriteResource(target);
    }

    clear_operations_.push_back({ target, color, ClearType::RenderTarget });

    // TODO: Route to graphics backend clear command
    LOG_F(9,
      "[CommandRecorder] Render target clear recorded - GPU execution pending");
  }

  auto ClearDepthStencilView(
    ResourceHandle target, float depth, uint8_t stencil) -> void override
  {
    if (context_ && context_->HasAsyncEngineIntegration()) {
      const auto& view_ctx = context_->GetViewInfo();
      LOG_F(3,
        "[CommandRecorder] Clearing depth stencil {} for view '{}' (depth: {}, "
        "stencil: {})",
        target.get(), view_ctx.view_name, depth, stencil);

      context_->AddWriteResource(target);
    }

    depth_clear_operations_.push_back({ target, depth, stencil });

    LOG_F(9, "[CommandRecorder] Depth stencil clear recorded");
  }

  auto SetGraphicsRootConstantBufferView(uint32_t index, uint64_t gpu_address)
    -> void override
  {
    if (context_ && context_->HasAsyncEngineIntegration()) {
      LOG_F(9, "[CommandRecorder] Setting root CBV {} at address 0x{:x}", index,
        gpu_address);
    }

    root_cbv_bindings_[index] = gpu_address;

    // TODO: Bind through graphics backend descriptor system
    LOG_F(9, "[CommandRecorder] Root CBV binding recorded");
  }

  auto SetGraphicsRoot32BitConstant(
    uint32_t index, uint32_t value, uint32_t offset) -> void override
  {
    if (context_ && context_->HasAsyncEngineIntegration()) {
      LOG_F(9, "[CommandRecorder] Setting root constant[{}][{}] = {}", index,
        offset, value);
    }

    root_constants_[{ index, offset }] = value;

    LOG_F(9, "[CommandRecorder] Root constant recorded");
  }

  auto DrawIndexedInstanced(uint32_t index_count, uint32_t instance_count,
    uint32_t start_index, int32_t base_vertex, uint32_t start_instance)
    -> void override
  {
    if (context_ && context_->HasAsyncEngineIntegration()) {
      const auto& view_ctx = context_->GetViewInfo();
      LOG_F(9,
        "[CommandRecorder] Draw indexed instanced for view '{}': {} indices, "
        "{} instances",
        view_ctx.view_name, index_count, instance_count);
    }

    draw_commands_.push_back({ index_count, instance_count, start_index,
      base_vertex, start_instance });

    total_draw_calls_++;
    total_primitives_ += (index_count / 3) * instance_count; // Assume triangles

    LOG_F(9, "[CommandRecorder] Draw command recorded (total: {})",
      total_draw_calls_);
  }

  auto Dispatch(uint32_t thread_group_count_x, uint32_t thread_group_count_y,
    uint32_t thread_group_count_z) -> void override
  {
    if (context_ && context_->HasAsyncEngineIntegration()) {
      LOG_F(9, "[CommandRecorder] Dispatch compute: {}x{}x{} thread groups",
        thread_group_count_x, thread_group_count_y, thread_group_count_z);
    }

    compute_dispatches_.push_back(
      { thread_group_count_x, thread_group_count_y, thread_group_count_z });

    total_dispatches_++;

    LOG_F(9, "[CommandRecorder] Compute dispatch recorded (total: {})",
      total_dispatches_);
  }

  auto CopyTexture(ResourceHandle source, ResourceHandle dest) -> void override
  {
    if (context_ && context_->HasAsyncEngineIntegration()) {
      LOG_F(
        9, "[CommandRecorder] Copy texture {} -> {}", source.get(), dest.get());

      context_->AddReadResource(source);
      context_->AddWriteResource(dest);
    }

    copy_operations_.push_back({ source, dest });
    total_copies_++;

    LOG_F(
      9, "[CommandRecorder] Texture copy recorded (total: {})", total_copies_);
  }

  [[nodiscard]] auto GetDebugInfo() const -> std::string override
  {
    std::ostringstream info;
    info << "AsyncEngineCommandRecorder["
         << "Draws: " << total_draw_calls_
         << ", Dispatches: " << total_dispatches_
         << ", Copies: " << total_copies_
         << ", Primitives: " << total_primitives_ << "]";
    return info.str();
  }

  // === COMMAND EXECUTION ===

  //! Execute all recorded commands (for immediate mode testing)
  auto ExecuteCommands() -> void
  {
    if (context_ && context_->HasAsyncEngineIntegration()) {
      const auto& view_ctx = context_->GetViewInfo();
      LOG_F(2, "[CommandRecorder] Executing {} commands for view '{}'",
        GetTotalCommandCount(), view_ctx.view_name);
    }

    // In real implementation, this would submit to graphics backend
    LOG_F(2,
      "[CommandRecorder] Command execution simulated - {} draw calls, {} "
      "dispatches",
      total_draw_calls_, total_dispatches_);
  }

  //! Get total number of recorded commands
  [[nodiscard]] auto GetTotalCommandCount() const -> uint32_t
  {
    return total_draw_calls_ + total_dispatches_ + total_copies_
      + static_cast<uint32_t>(clear_operations_.size());
  }

private:
  enum class ClearType { RenderTarget, DepthStencil };

  struct ClearOperation {
    ResourceHandle target;
    std::vector<float> color;
    ClearType type;
  };

  struct DepthClearOperation {
    ResourceHandle target;
    float depth;
    uint8_t stencil;
  };

  struct DrawCommand {
    uint32_t index_count;
    uint32_t instance_count;
    uint32_t start_index;
    int32_t base_vertex;
    uint32_t start_instance;
  };

  struct ComputeDispatch {
    uint32_t thread_group_x;
    uint32_t thread_group_y;
    uint32_t thread_group_z;
  };

  struct CopyOperation {
    ResourceHandle source;
    ResourceHandle dest;
  };

  TaskExecutionContext* context_;

  // Recorded state and commands
  std::vector<float> current_viewport_;
  void* current_pso_ { nullptr };
  std::unordered_map<uint32_t, uint64_t> root_cbv_bindings_;
  std::unordered_map<std::pair<uint32_t, uint32_t>, uint32_t> root_constants_;

  std::vector<ClearOperation> clear_operations_;
  std::vector<DepthClearOperation> depth_clear_operations_;
  std::vector<DrawCommand> draw_commands_;
  std::vector<ComputeDispatch> compute_dispatches_;
  std::vector<CopyOperation> copy_operations_;

  // Statistics
  uint32_t total_draw_calls_ { 0 };
  uint32_t total_dispatches_ { 0 };
  uint32_t total_copies_ { 0 };
  uint32_t total_primitives_ { 0 };
};

// NOTE: AsyncEngineTaskExecutionContext full implementation lives only in
// header now. This file only supplies the factory and supporting recorder
// implementation.

//! Factory function to create enhanced execution context
auto CreateAsyncEngineTaskExecutionContext()
  -> std::unique_ptr<TaskExecutionContext>
{
  return std::make_unique<AsyncEngineTaskExecutionContext>();
}

} // namespace oxygen::engine::asyncsim
