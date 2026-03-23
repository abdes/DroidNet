//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/ReadbackValidation.h>

#include <Oxygen/Core/Detail/FormatUtils.h>

using oxygen::graphics::detail::FormatInfo;
using oxygen::graphics::detail::GetFormatInfo;

namespace oxygen::graphics {
namespace {

  auto HasExactlyOneAspect(const ClearFlags aspects) -> bool
  {
    const auto bits = static_cast<uint8_t>(aspects);
    return bits != 0U && (bits & (bits - 1U)) == 0U;
  }

  auto IsColorAspect(const ClearFlags aspects) -> bool
  {
    return aspects == ClearFlags::kColor;
  }

  auto IsDepthAspect(const ClearFlags aspects) -> bool
  {
    return aspects == ClearFlags::kDepth;
  }

  auto IsStencilAspect(const ClearFlags aspects) -> bool
  {
    return aspects == ClearFlags::kStencil;
  }

} // namespace

auto ValidateTextureReadbackRequest(const TextureDesc& desc,
  const TextureReadbackRequest& request) -> std::expected<void, ReadbackError>
{
  if (desc.format == oxygen::Format::kUnknown || desc.is_typeless) {
    return std::unexpected(ReadbackError::kUnsupportedFormat);
  }
  if (!HasExactlyOneAspect(request.aspects)) {
    return std::unexpected(ReadbackError::kInvalidArgument);
  }

  const FormatInfo& format_info = GetFormatInfo(desc.format);
  if (IsColorAspect(request.aspects)) {
    if (format_info.has_depth || format_info.has_stencil) {
      return std::unexpected(ReadbackError::kInvalidArgument);
    }
  } else if (IsDepthAspect(request.aspects)) {
    if (!format_info.has_depth) {
      return std::unexpected(ReadbackError::kInvalidArgument);
    }
    return std::unexpected(ReadbackError::kUnsupportedResource);
  } else if (IsStencilAspect(request.aspects)) {
    if (!format_info.has_stencil) {
      return std::unexpected(ReadbackError::kInvalidArgument);
    }
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }

  if (desc.sample_count > 1
    && request.msaa_mode == MsaaReadbackMode::kDisallow) {
    return std::unexpected(ReadbackError::kUnsupportedResource);
  }

  return {};
}

} // namespace oxygen::graphics
