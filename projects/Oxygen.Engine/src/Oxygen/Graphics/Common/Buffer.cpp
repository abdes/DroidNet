//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>

using oxygen::graphics::Buffer;
using oxygen::graphics::BufferViewDescription;

auto Buffer::GetNativeView(const DescriptorHandle& view_handle,
  const BufferViewDescription& view_desc) const -> NativeView
{
  using graphics::ResourceViewType;

  switch (view_desc.view_type) {
  case ResourceViewType::kConstantBuffer:
    return CreateConstantBufferView(view_handle, view_desc.range);
  case ResourceViewType::kRawBuffer_SRV:
  case ResourceViewType::kTypedBuffer_SRV:
  case ResourceViewType::kStructuredBuffer_SRV:
    return CreateShaderResourceView(
      view_handle, view_desc.format, view_desc.range, view_desc.stride);
  case ResourceViewType::kRawBuffer_UAV:
  case ResourceViewType::kTypedBuffer_UAV:
  case ResourceViewType::kStructuredBuffer_UAV:
    return CreateUnorderedAccessView(
      view_handle, view_desc.format, view_desc.range, view_desc.stride);
  default:
    // Unknown or unsupported view type
    return {};
  }
}
