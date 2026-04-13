//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <memory>

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

OXGN_VRTX_API auto EnsureBufferAndSrv(Graphics& gfx,
  std::shared_ptr<graphics::Buffer>& buffer, ShaderVisibleIndex& bindless_index,
  std::uint64_t size_bytes, std::uint32_t stride, std::string_view debug_label,
  bindless::DomainToken domain = bindless::generated::kGlobalSrvDomain)
  -> std::expected<EnsureBufferResult, std::error_code>;

} // namespace oxygen::vortex::upload::internal
