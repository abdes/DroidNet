//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/glm.hpp>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ViewPort.h>

namespace oxygen {

// Unique identifier for a view within a frame
using ViewIdTag = struct ViewIdTag;
using ViewId = NamedType<uint64_t, ViewIdTag, Comparable, Hashable, Printable>;

//! Convert a VersionedBindlessHandle to a human-readable string.
OXGN_CORE_NDAPI auto to_string(const ViewId& v) -> std::string;

//! Lightweight view configuration (no matrices).
/*!
 This struct holds per-view configuration: viewport, scissor rectangle,
 pixel jitter (in pixels), and flags. It intentionally does not contain
 camera matrices or derived cached data; those belong in `ResolvedView`.
 */
struct View {
  ViewPort viewport {};
  Scissors scissor {};
  // Pixel jitter in pixels (sub-pixel values allowed). Positive X = right,
  // positive Y = down (top-left origin). The ViewResolver converts pixels
  // -> NDC when applying jitter.
  glm::vec2 pixel_jitter { 0.0f, 0.0f };
  bool reverse_z = false; // if true, projection uses reversed-Z (near > far)
  bool mirrored = false;

  View() = default;
};

} // namespace oxygen
