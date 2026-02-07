//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <glm/glm.hpp>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Internal/GpuDebugManager.h>

namespace oxygen::engine::internal {

struct GpuDebugLine {
  glm::vec4 world_pos0;
  glm::vec4 world_pos1;
  glm::vec4 color_alpha0;
  glm::vec4 color_alpha1;
};

GpuDebugManager::GpuDebugManager(observer_ptr<Graphics> gfx)
  : gfx_(gfx)
{
  const uint32_t max_lines = 128 * 1024;

  // 1. Create Line Buffer (Structured Buffer UAV/SRV)
  graphics::BufferDesc line_desc {
    .size_bytes = max_lines * sizeof(GpuDebugLine),
    .usage = graphics::BufferUsage::kStorage,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = "GpuDebugLineBuffer",
  };
  line_buffer_ = gfx_->CreateBuffer(line_desc);

  // 2. Create Counter Buffer (Raw Buffer UAV + Indirect Args)
  // D3D12_DRAW_ARGUMENTS is 4 * UINT32
  constexpr size_t kCounterBufferSize = 4 * sizeof(uint32_t);
  graphics::BufferDesc counter_desc {
    .size_bytes = kCounterBufferSize,
    .usage = graphics::BufferUsage::kStorage | graphics::BufferUsage::kIndirect,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = "GpuDebugCounterBuffer",
  };
  counter_buffer_ = gfx_->CreateBuffer(counter_desc);

  auto& registry = gfx_->GetResourceRegistry();
  registry.Register(line_buffer_);
  registry.Register(counter_buffer_);

  auto& allocator = gfx_->GetDescriptorAllocator();

  // 3. Create Views
  // Line Buffer SRV
  {
    graphics::BufferViewDescription srv_desc {
      .view_type = graphics::ResourceViewType::kStructuredBuffer_SRV,
      .stride = sizeof(GpuDebugLine),
    };
    auto handle = allocator.Allocate(
      srv_desc.view_type, graphics::DescriptorVisibility::kShaderVisible);
    line_buffer_srv_ = allocator.GetShaderVisibleIndex(handle);
    registry.RegisterView(*line_buffer_, std::move(handle), srv_desc);
  }

  // Line Buffer UAV
  {
    graphics::BufferViewDescription uav_desc {
      .view_type = graphics::ResourceViewType::kStructuredBuffer_UAV,
      .stride = sizeof(GpuDebugLine),
    };
    auto handle = allocator.Allocate(
      uav_desc.view_type, graphics::DescriptorVisibility::kShaderVisible);
    line_buffer_uav_ = allocator.GetShaderVisibleIndex(handle);
    registry.RegisterView(*line_buffer_, std::move(handle), uav_desc);
  }

  // Counter Buffer UAV (Raw)
  {
    graphics::BufferViewDescription uav_desc {
      .view_type = graphics::ResourceViewType::kRawBuffer_UAV,
    };
    auto handle = allocator.Allocate(
      uav_desc.view_type, graphics::DescriptorVisibility::kShaderVisible);
    counter_buffer_uav_ = allocator.GetShaderVisibleIndex(handle);
    registry.RegisterView(*counter_buffer_, std::move(handle), uav_desc);
  }
}

GpuDebugManager::~GpuDebugManager() = default;

void GpuDebugManager::OnFrameStart(graphics::CommandRecorder& /*recorder*/)
{
  // Resetting is handled by GpuDebugClearPass
}

} // namespace oxygen::engine::internal
