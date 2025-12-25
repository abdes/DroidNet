//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <system_error>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/Errors.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Upload/UploadHelpers.h>

using oxygen::graphics::GraphicsError;

namespace oxygen::engine::upload::internal {

auto EnsureBufferAndSrv(Graphics& gfx,
  std::shared_ptr<graphics::Buffer>& buffer, ShaderVisibleIndex& bindless_index,
  std::uint64_t size_bytes, const std::uint32_t stride,
  std::string_view debug_label)
  -> std::expected<EnsureBufferResult, std::error_code>
{
  if (buffer && buffer->GetSize() >= size_bytes) {
    return std::expected<EnsureBufferResult, GraphicsError>(
      EnsureBufferResult::kUnchanged);
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
    if (!new_buffer || new_buffer->GetSize() != size_bytes) {
      LOG_F(ERROR, "-failed- to create new buffer resource");
      return std::unexpected(
        make_error_code(GraphicsError::kResourceCreationFailed));
    }
  } catch (const std::exception& e) {
    LOG_F(ERROR, "-failed- to create new buffer resource with exception: {}",
      e.what());
    return std::unexpected(
      make_error_code(GraphicsError::kResourceCreationFailed));
  }
  DLOG_F(2, "new buffer resource created");

  graphics::BufferViewDescription view_desc;
  view_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
  view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  view_desc.range = { 0, size_bytes };
  view_desc.stride = stride;

  auto& registry = gfx.GetResourceRegistry();
  const bool had_old = static_cast<bool>(buffer);

  if (!had_old) {
    // First-time creation: allocate descriptor, register resource, register
    // view.
    oxygen::graphics::DescriptorHandle view_handle {};
    ShaderVisibleIndex sv_index { kInvalidShaderVisibleIndex };
    try {
      auto& allocator = gfx.GetDescriptorAllocator();
      view_handle
        = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
          graphics::DescriptorVisibility::kShaderVisible);
      // Validate that we received a valid descriptor handle
      if (!view_handle.IsValid()) {
        LOG_F(ERROR, "-failed- to allocate valid SRV descriptor handle");
        return std::unexpected(
          make_error_code(GraphicsError::kDescriptorAllocationFailed));
      }
      sv_index = allocator.GetShaderVisibleIndex(view_handle);
    } catch (const std::exception& e) {
      LOG_F(ERROR, "-failed- to allocate SRV with exception: {}", e.what());
      return std::unexpected(
        make_error_code(GraphicsError::kDescriptorAllocationFailed));
    }

    // Register resource and view with ResourceRegistry.
    // Note: ResourceRegistry contract violations (like duplicate registration
    // or invalid handles) will abort the program rather than throw exceptions.
    // Our validation above should prevent such violations.
    try {
      registry.Register(new_buffer);
      registry.RegisterView(*new_buffer, std::move(view_handle), view_desc);
    } catch (const std::exception& e) {
      LOG_F(ERROR, "-failed- to register new buffer or view: {}", e.what());
      return std::unexpected(
        make_error_code(GraphicsError::kResourceRegistrationFailed));
    }

    buffer = std::move(new_buffer);
    bindless_index = ShaderVisibleIndex(sv_index.get());
    return std::expected<EnsureBufferResult, GraphicsError>(
      EnsureBufferResult::kCreated);
  }

  // Resize path: keep the same bindless index by replacing resource and
  // recreating the SRV in-place via the callback-based Replace.
  // Note: ResourceRegistry.Replace() handles resource swapping atomically and
  // preserves bindless indices as required for the resize use case.
  try {
    // Ensure the old buffer is kept alive until the GPU is done with it.
    gfx.GetDeferredReclaimer().RegisterDeferredRelease(buffer);

    registry.Replace(
      *buffer, new_buffer, [&](const graphics::BufferViewDescription&) {
        return std::optional<graphics::BufferViewDescription>(view_desc);
      });
    // new_buffer is now owned by registry; update our shared_ptr to match.
    buffer = std::move(new_buffer);
  } catch (const std::exception& e) {
    LOG_F(ERROR, "-failed- to replace buffer: {}", e.what());
    return std::unexpected(
      make_error_code(GraphicsError::kResourceRegistrationFailed));
  }

  return std::expected<EnsureBufferResult, GraphicsError>(
    EnsureBufferResult::kResized);
}

} // namespace oxygen::engine::upload::internal
