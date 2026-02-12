//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/View.h>

namespace oxygen::engine::internal {

//! Interface for providing captured sky environment cubemaps.
class ISkyCaptureProvider {
public:
  virtual ~ISkyCaptureProvider() = default;

  //! Returns the shader-visible SRV index for the captured cubemap.
  [[nodiscard]] virtual auto GetCapturedCubemapSlot(
    ViewId view_id) const noexcept
    -> ShaderVisibleIndex
    = 0;

  //! Returns true if the sky has been captured at least once and is ready.
  [[nodiscard]] virtual auto IsCaptured(ViewId view_id) const noexcept -> bool
    = 0;

  //! Returns a monotonic generation token that increases when the capture
  //! has been updated.
  [[nodiscard]] virtual auto GetCaptureGeneration(ViewId view_id) const noexcept
    -> std::uint64_t
    = 0;
};

} // namespace oxygen::engine::internal
