//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/api_export.h>

namespace oxygen::transforms {

//! Where a node should face: a direction and the up it leans toward.
struct LookDirection {
  //! World direction the node faces; need not be normalized.
  Vec3 forward;
  //! World direction the node's up leans toward.
  Vec3 up { space::move::Up };
};

//! The node rotation that faces `direction.forward` with its up as close to
//! `direction.up` as the forward allows.
/*!
 The rotation maps `space::node::Forward` onto the forward and
 `space::node::Up` onto the up made perpendicular to it. A zero forward keeps
 the node frame's forward; an up parallel to the forward (or zero) falls back
 to `space::move::Up`, then to `space::move::Back` when the forward is
 vertical.

 @param direction The forward and up to face.
 @return A unit rotation quaternion.
*/
OXGN_CORE_NDAPI auto LookRotation(const LookDirection& direction) noexcept
  -> Quat;

//! The view matrix of a camera node at `position` with world `rotation`.
/*!
 The camera looks along its node's forward (`space::node::Forward`), with its
 node's up at the top of the image and its node's right on the right: the
 matrix maps them onto view space's `space::look` axes.

 @param position World position of the camera node.
 @param rotation World rotation of the camera node.
 @return The world-to-view matrix.
*/
OXGN_CORE_NDAPI auto NodeViewMatrix(
  const Vec3& position, const Quat& rotation) noexcept -> Mat4;

} // namespace oxygen::transforms
