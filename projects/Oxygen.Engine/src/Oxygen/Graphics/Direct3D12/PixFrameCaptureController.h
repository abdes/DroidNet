//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics::d3d12 {

class Graphics;

OXGN_D3D12_NDAPI auto CreatePixFrameCaptureController(
  Graphics& graphics, const FrameCaptureConfig& config)
  -> std::unique_ptr<graphics::FrameCaptureController>;

} // namespace oxygen::graphics::d3d12
