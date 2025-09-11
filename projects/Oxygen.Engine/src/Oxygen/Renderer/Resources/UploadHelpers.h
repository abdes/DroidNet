//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Core/Types/BindlessHandle.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
namespace graphics {
  class Buffer;
} // namespace graphics
} // namespace oxygen::graphics

namespace oxygen::renderer::resources::internal {

OXGN_RNDR_API auto EnsureBufferAndSrv(Graphics& gfx,
  std::shared_ptr<graphics::Buffer>& buffer, ShaderVisibleIndex& bindless_index,
  std::uint64_t size_bytes, std::uint32_t stride, std::string_view debug_label)
  -> bool;

} // namespace oxygen::renderer::resources::internal
