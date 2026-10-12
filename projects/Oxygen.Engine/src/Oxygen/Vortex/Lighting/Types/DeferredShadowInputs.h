//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowFrameData.h>

namespace oxygen::vortex {

//! The shadow products one view's deferred lights sample.
/*!
 Null `data` means the view published no shadows; the surface spans are then
 empty.
*/
struct DeferredShadowInputs {
  observer_ptr<const ShadowFrameData> data;
  std::span<const std::shared_ptr<graphics::Texture>> directional_surfaces;
  std::span<const std::shared_ptr<graphics::Texture>> spot_surfaces;
  std::span<const std::shared_ptr<graphics::Texture>> point_surfaces;
};

} // namespace oxygen::vortex
