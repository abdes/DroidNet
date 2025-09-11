//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
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

//! Result of EnsureBufferAndSrv when it succeeds.
enum class EnsureBufferResult {
  kUnchanged, //!< existing buffer already large enough
  kCreated, //!< buffer was created new (no previous buffer)
  kResized //!< an existing buffer was replaced with a larger one
};

OXGN_RNDR_API auto EnsureBufferAndSrv(Graphics& gfx,
  std::shared_ptr<graphics::Buffer>& buffer, ShaderVisibleIndex& bindless_index,
  std::uint64_t size_bytes, std::uint32_t stride, std::string_view debug_label)
  -> std::expected<EnsureBufferResult, std::error_code>;

} // namespace oxygen::renderer::resources::internal
