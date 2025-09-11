//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Resources/UploadHelpers.h>

namespace oxygen::renderer::resources::internal {

auto EnsureBufferAndSrv(Graphics& gfx,
  std::shared_ptr<graphics::Buffer>& buffer, ShaderVisibleIndex& bindless_index,
  std::uint64_t size_bytes, const std::uint32_t stride,
  std::string_view debug_label) -> bool
{
  if (buffer && buffer->GetSize() >= size_bytes) {
    return true;
  }

  DLOG_SCOPE_F(2, fmt::format("EnsureBufferAndSrv: '{}'", debug_label).c_str());
  DLOG_F(2, "requested size  : {} bytes", size_bytes);
  DLOG_F(2, "stride          : {} bytes", stride);
  DLOG_F(2, "existing buffer : {}{}", buffer ? "yes" : "no",
    buffer ? fmt::format(" ({})", buffer->GetSize()) : "");

  // Create new buffer first
  graphics::BufferDesc desc;
  desc.size_bytes = size_bytes;
  desc.usage = graphics::BufferUsage::kStorage;
  desc.memory = graphics::BufferMemory::kDeviceLocal;
  desc.debug_name = debug_label.data();

  std::shared_ptr<graphics::Buffer> new_buffer;
  try {
    new_buffer = gfx.CreateBuffer(desc);
    if (!new_buffer) {
      LOG_F(ERROR, "-failed- to create new buffer resource");
      return false;
    }
  } catch (const std::exception& e) {
    LOG_F(ERROR, "-failed- to create new buffer resource with exception: {}",
      e.what());
    return false;
  }
  DLOG_F(2, "new buffer resource created");

  graphics::BufferViewDescription view_desc;
  view_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
  view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  view_desc.range = { 0, size_bytes };
  view_desc.stride = stride;

  // At this point we have a new buffer object. Allocate a new descriptor,
  // register the new buffer and its view, then unregister the old buffer.
  // Allocate descriptor, register the new buffer and its view, then
  // unregister the old buffer. This logic was previously in
  // RegisterNewBufferAndView and is inlined here to reduce indirection.
  try {
    auto& allocator = gfx.GetDescriptorAllocator();
    auto view_handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    auto sv_index = allocator.GetShaderVisibleIndex(view_handle);

    auto& registry = gfx.GetResourceRegistry();
    registry.Register(new_buffer);
    registry.RegisterView(*new_buffer, std::move(view_handle), view_desc);

    // Unregister old buffer if present, then move new buffer into place
    if (buffer) {
      registry.UnRegisterResource(*buffer);
    }
    buffer = std::move(new_buffer);
    bindless_index = ShaderVisibleIndex(sv_index.get());
  } catch (const std::exception& e) {
    LOG_F(ERROR, "-failed- to allocate SRV with exception: {}", e.what());
    return false;
  }
  return true;
}

} // namespace oxygen::renderer::resources::internal
