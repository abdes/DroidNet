//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <dxgi.h>

#include <Oxygen/Graphics/Common/Detail/FormatUtils.h>

namespace oxygen::graphics::d3d12::detail {

struct DxgiFormatMapping {
    Format generic_format;
    DXGI_FORMAT resource_format;
    DXGI_FORMAT srv_format;
    DXGI_FORMAT rtv_format;
};

auto GetDxgiFormatMapping(Format generic_format) -> const DxgiFormatMapping&;

} // namespace oxygen::graphics::d3d12::detail
