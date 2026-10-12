//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>
#include <system_error>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen {
class Graphics;
namespace graphics {
  class Buffer;
} // namespace graphics
} // namespace oxygen::graphics

namespace oxygen::vortex::upload::internal {

//! A new structured buffer with its shader-visible SRV.
struct BufferWithSrv {
  std::shared_ptr<graphics::Buffer> buffer;
  ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
};

//! What CreateBufferAndSrv creates.
struct StructuredBufferSpec {
  std::uint64_t size_bytes { 0U };
  std::uint32_t stride { 0U };
  std::string_view debug_label;
  bindless::DomainToken domain { bindless::generated::kGlobalSrvDomain };
};

//! Creates a device-local structured buffer and a new bindless SRV for it.
/*!
 Every call returns a new buffer and a new descriptor. Published descriptors
 are never repointed: a caller replacing an older buffer publishes the new
 SRV index and retires the old buffer with `Graphics::RetireResource`.
*/
OXGN_VRTX_API auto CreateBufferAndSrv(
  Graphics& gfx, const StructuredBufferSpec& spec)
  -> std::expected<BufferWithSrv, std::error_code>;

} // namespace oxygen::vortex::upload::internal
