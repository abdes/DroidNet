//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Vortex/Internal/StructuredGpuBuffer.h>

namespace oxygen::vortex::internal {

namespace {

  auto RetireBuffer(Graphics& gfx, std::shared_ptr<graphics::Buffer> buffer)
    -> void
  {
    gfx.GetDeferredReclaimer().RegisterDeferredAction(
      [owner = &gfx, retired = std::move(buffer)] mutable -> void {
        owner->ForgetKnownResourceState(*retired);
        auto& registry = owner->GetResourceRegistry();
        if (registry.Contains(*retired)) {
          registry.UnRegisterResource(*retired);
        }
        retired.reset();
      });
  }

  //! Registers one shader-visible view of `buffer`; invalid on failure.
  auto RegisterView(Graphics& gfx, const graphics::Buffer& buffer,
    const graphics::ResourceViewType view_type, const std::uint32_t stride)
    -> ShaderVisibleIndex
  {
    auto& allocator = gfx.GetDescriptorAllocator();
    auto handle = view_type == graphics::ResourceViewType::kStructuredBuffer_SRV
      ? allocator.AllocateBindless(
          bindless::generated::kGlobalSrvDomain, view_type)
      : allocator.AllocateRaw(
          view_type, graphics::DescriptorVisibility::kShaderVisible);
    if (!handle.IsValid()) {
      return kInvalidShaderVisibleIndex;
    }
    const auto index = allocator.GetShaderVisibleIndex(handle);
    const auto view
      = gfx.GetResourceRegistry().RegisterView(buffer, std::move(handle),
        graphics::BufferViewDescription {
          .view_type = view_type,
          .visibility = graphics::DescriptorVisibility::kShaderVisible,
          .range = { 0U, buffer.GetSize() },
          .stride = stride,
        });
    return view->IsValid() ? index : kInvalidShaderVisibleIndex;
  }

} // namespace

StructuredGpuBuffer::StructuredGpuBuffer(std::string debug_name,
  const std::uint32_t stride, const graphics::BufferUsage extra_usage)
  : debug_name_(std::move(debug_name))
  , stride_(stride)
  , usage_(graphics::BufferUsage::kStorage | extra_usage)
{
  CHECK_F(stride_ != 0U, "{}: structured buffers need a stride", debug_name_);
}

StructuredGpuBuffer::~StructuredGpuBuffer() { Retire(); }

auto StructuredGpuBuffer::Ensure(const std::shared_ptr<Graphics>& gfx,
  const std::uint32_t element_count) -> bool
{
  CHECK_NOTNULL_F(gfx.get(), "{}: Ensure requires Graphics", debug_name_);
  if (buffer_ != nullptr && capacity_ >= element_count) {
    return true;
  }

  Retire();
  gfx_ = gfx;
  const auto capacity = (std::max)(element_count, 1U);
  auto buffer = gfx->CreateBuffer(graphics::BufferDesc {
    .size_bytes = static_cast<std::uint64_t>(capacity) * stride_,
    .usage = usage_,
    .memory = graphics::BufferMemory::kDeviceLocal,
    .debug_name = debug_name_,
  });
  if (buffer == nullptr) {
    LOG_F(ERROR, "{}: buffer creation failed", debug_name_);
    return false;
  }
  buffer->SetName(debug_name_);
  gfx->GetResourceRegistry().Register(buffer);

  const auto srv = RegisterView(
    *gfx, *buffer, graphics::ResourceViewType::kStructuredBuffer_SRV, stride_);
  const auto uav = RegisterView(
    *gfx, *buffer, graphics::ResourceViewType::kStructuredBuffer_UAV, stride_);
  if (!srv.IsValid() || !uav.IsValid()) {
    LOG_F(ERROR, "{}: view registration failed", debug_name_);
    RetireBuffer(*gfx, std::move(buffer));
    return false;
  }

  buffer_ = std::move(buffer);
  srv_ = srv;
  uav_ = uav;
  capacity_ = capacity;
  return true;
}

auto StructuredGpuBuffer::Retire() -> void
{
  if (auto gfx = gfx_.lock(); gfx != nullptr && buffer_ != nullptr) {
    RetireBuffer(*gfx, std::move(buffer_));
  }
  buffer_.reset();
  srv_ = kInvalidShaderVisibleIndex;
  uav_ = kInvalidShaderVisibleIndex;
  capacity_ = 0U;
}

} // namespace oxygen::vortex::internal
