//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/ShadowManager.h>

namespace oxygen::engine::detail {

template <std::size_t SlotCount> class VirtualShadowPassConstantBufferOwner {
public:
  VirtualShadowPassConstantBufferOwner()
  {
    indices_.fill(kInvalidShaderVisibleIndex);
  }

  ~VirtualShadowPassConstantBufferOwner() { Reset(); }

  OXYGEN_MAKE_NON_COPYABLE(VirtualShadowPassConstantBufferOwner)
  OXYGEN_DEFAULT_MOVABLE(VirtualShadowPassConstantBufferOwner)

  auto Reset() -> void
  {
    if (buffer_ && mapped_ptr_ != nullptr) {
      buffer_->UnMap();
      mapped_ptr_ = nullptr;
    }
  }

  auto Ensure(Graphics& gfx, const std::string_view debug_name,
    const std::uint32_t stride) -> void
  {
    if (buffer_ && mapped_ptr_ != nullptr && indices_[0].IsValid()) {
      return;
    }

    auto& allocator = gfx.GetDescriptorAllocator();
    auto& registry = gfx.GetResourceRegistry();
    const graphics::BufferDesc desc {
      .size_bytes = static_cast<std::uint64_t>(stride) * SlotCount,
      .usage = graphics::BufferUsage::kConstant,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = std::string(debug_name),
    };
    buffer_ = gfx.CreateBuffer(desc);
    registry.Register(buffer_);

    mapped_ptr_ = buffer_->Map(0U, desc.size_bytes);

    indices_.fill(kInvalidShaderVisibleIndex);
    for (std::size_t slot = 0U; slot < SlotCount; ++slot) {
      auto cbv_handle
        = allocator.Allocate(graphics::ResourceViewType::kConstantBuffer,
          graphics::DescriptorVisibility::kShaderVisible);
      indices_[slot] = allocator.GetShaderVisibleIndex(cbv_handle);

      graphics::BufferViewDescription cbv_desc;
      cbv_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
      cbv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
      cbv_desc.range = {
        static_cast<std::uint32_t>(slot * stride),
        stride,
      };
      cbvs_[slot] = registry.RegisterView(
        *buffer_, std::move(cbv_handle), cbv_desc);
    }
  }

  [[nodiscard]] auto MappedPtr() const noexcept -> void* { return mapped_ptr_; }

  [[nodiscard]] auto Index(const std::size_t slot) const noexcept
    -> ShaderVisibleIndex
  {
    return indices_[slot];
  }

private:
  std::shared_ptr<graphics::Buffer> buffer_ {};
  std::array<graphics::NativeView, SlotCount> cbvs_ {};
  std::array<ShaderVisibleIndex, SlotCount> indices_ {};
  void* mapped_ptr_ { nullptr };
};

inline auto DispatchVirtualPageManagementPass(renderer::ShadowManager& shadow_manager,
  const ViewId view_id, graphics::CommandRecorder& recorder,
  const std::uint32_t group_count) -> void
{
  shadow_manager.PrepareVirtualPageManagementOutputsForGpuWrite(
    view_id, recorder);
  recorder.Dispatch(group_count, 1U, 1U);
}

template <typename BuildConstantsFn>
auto WritePerClipPassConstants(void* mapped_ptr, const std::size_t base_slot,
  const std::uint32_t stride, const std::uint32_t clip_level_count,
  BuildConstantsFn&& build_constants) -> std::uint32_t
{
  auto* bytes = static_cast<std::byte*>(mapped_ptr);
  std::uint32_t dispatch_slot = 0U;
  for (std::uint32_t clip_index = clip_level_count - 1U; clip_index-- > 0U;) {
    const auto slot = base_slot + dispatch_slot;
    const auto constants = std::forward<BuildConstantsFn>(build_constants)(
      clip_index);
    auto* slot_ptr
      = bytes + static_cast<std::ptrdiff_t>(slot * static_cast<std::size_t>(stride));
    std::memcpy(slot_ptr, &constants, sizeof(constants));
    ++dispatch_slot;
  }
  return dispatch_slot;
}

} // namespace oxygen::engine::detail
