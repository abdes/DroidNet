//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>

#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

OXGN_GFX_API auto ValidateTextureReadbackRequest(const TextureDesc& desc,
  const TextureReadbackRequest& request) -> std::expected<void, ReadbackError>;

} // namespace oxygen::graphics
