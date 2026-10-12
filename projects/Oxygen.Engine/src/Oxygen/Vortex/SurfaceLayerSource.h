//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>

namespace oxygen::graphics {
class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen::vortex {

//! Screen-space UI drawn over everything composited on a surface.
/*!
 For each active source whose surface is composited this frame, the renderer
 binds and clears an `RGBA8_UNORM` layer sized to the composite target, calls
 `Record`, and alpha-blends the layer above every view layer. The source owns
 its frame lifecycle; the renderer only asks whether it has something to draw.

 The renderer calls sources on the render thread during compositing only.
*/
class SurfaceLayerSource {
public:
  SurfaceLayerSource() = default;
  virtual ~SurfaceLayerSource() = default;

  OXYGEN_DEFAULT_COPYABLE(SurfaceLayerSource)
  OXYGEN_DEFAULT_MOVABLE(SurfaceLayerSource)

  //! False skips the layer this frame (no UI frame, no draw data).
  [[nodiscard]] virtual auto IsActive() const noexcept -> bool = 0;

  //! Records UI draws; the layer framebuffer is bound and cleared.
  virtual auto Record(graphics::CommandRecorder& recorder) -> void = 0;
};

//! Identifies one registration of a surface layer source.
using SurfaceLayerToken
  = NamedType<std::uint64_t, struct SurfaceLayerTokenTag, Comparable, Hashable>;

//! Never returned by a registration.
inline constexpr SurfaceLayerToken kInvalidSurfaceLayerToken { 0U };

} // namespace oxygen::vortex
