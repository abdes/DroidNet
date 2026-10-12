//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <expected>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Errors.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploadHelpers.h>

using oxygen::graphics::GraphicsError;

namespace oxygen::vortex::upload::internal {

namespace {

  template <typename Resource>
  auto RegisterResourceIfNeeded(
    Graphics& gfx, const std::shared_ptr<Resource>& resource) -> void
  {
    if (!resource) {
      return;
    }

    auto& registry = gfx.GetResourceRegistry();
    if (!registry.Contains(*resource)) {
      registry.Register(resource);
    }
  }

} // namespace

auto CreateBufferAndSrv(Graphics& gfx, const StructuredBufferSpec& spec)
  -> std::expected<BufferWithSrv, std::error_code>
{
  DLOG_SCOPE_F(2, "CreateBufferAndSrv: '{}'", spec.debug_label);
  DLOG_F(2, "size   : {} bytes", spec.size_bytes);
  DLOG_F(2, "stride : {} bytes", spec.stride);

  graphics::BufferDesc desc;
  desc.size_bytes = spec.size_bytes;
  desc.usage = graphics::BufferUsage::kStorage;
  desc.memory = graphics::BufferMemory::kDeviceLocal;
  desc.debug_name = std::string { spec.debug_label };

  std::shared_ptr<graphics::Buffer> buffer;
  try {
    buffer = gfx.CreateBuffer(desc);
  } catch (const std::exception& e) {
    LOG_F(
      ERROR, "-failed- to create buffer '{}': {}", spec.debug_label, e.what());
    return std::unexpected(
      make_error_code(GraphicsError::kResourceCreationFailed));
  }
  if (!buffer || buffer->GetSize() != spec.size_bytes) {
    LOG_F(ERROR, "-failed- to create buffer '{}'", spec.debug_label);
    return std::unexpected(
      make_error_code(GraphicsError::kResourceCreationFailed));
  }

  graphics::BufferViewDescription view_desc;
  view_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
  view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  view_desc.range = { 0, spec.size_bytes };
  view_desc.stride = spec.stride;

  auto& allocator = gfx.GetDescriptorAllocator();
  graphics::DescriptorAllocationHandle view_handle;
  try {
    view_handle = allocator.AllocateBindless(
      spec.domain, graphics::ResourceViewType::kStructuredBuffer_SRV);
  } catch (const std::exception& e) {
    LOG_F(ERROR, "-failed- to allocate SRV for '{}': {}", spec.debug_label,
      e.what());
    return std::unexpected(
      make_error_code(GraphicsError::kDescriptorAllocationFailed));
  }
  if (!view_handle.IsValid()) {
    LOG_F(ERROR, "-failed- to allocate SRV for '{}'", spec.debug_label);
    return std::unexpected(
      make_error_code(GraphicsError::kDescriptorAllocationFailed));
  }
  const auto srv = allocator.GetShaderVisibleIndex(view_handle);

  try {
    RegisterResourceIfNeeded(gfx, buffer);
    gfx.GetResourceRegistry().RegisterView(
      *buffer, std::move(view_handle), view_desc);
  } catch (const std::exception& e) {
    LOG_F(ERROR, "-failed- to register buffer '{}' or its SRV: {}",
      spec.debug_label, e.what());
    return std::unexpected(
      make_error_code(GraphicsError::kResourceRegistrationFailed));
  }

  return BufferWithSrv { .buffer = std::move(buffer), .srv = srv };
}

} // namespace oxygen::vortex::upload::internal
