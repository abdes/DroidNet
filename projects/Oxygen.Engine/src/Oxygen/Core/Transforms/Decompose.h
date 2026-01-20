//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/fwd.hpp>

#include <Oxygen/Core/Transforms/IsFinite.h>
#include <Oxygen/Core/api_export.h>

namespace oxygen::transforms {

//! Strict TRS decomposition with no fallbacks.
/*!
 Returns false when the matrix is non-finite or cannot be decomposed into
 a valid TRS representation.
*/
OXGN_CORE_NDAPI auto TryDecomposeTransform(const glm::mat4& transform,
  glm::vec3& translation, glm::quat& rotation, glm::vec3& scale) -> bool;

//! Decompose a transform and apply a best-effort fallback when needed.
/*!
 Always succeeds. Non-finite input yields identity TRS output.
 If any axis scale is near zero, the rotation is set to identity while
 preserving the extracted translation and scale (this degenerate case is
 treated as valid).
 If the derived rotation is non-finite, the rotation is set to identity.

 @return True when the fallback path is used. Near-zero scale does not
   trigger a fallback.
*/
OXGN_CORE_API auto DecomposeTransformOrFallback(const glm::mat4& transform,
  glm::vec3& translation, glm::quat& rotation, glm::vec3& scale) -> bool;

//! Check for near-uniform scale.
OXGN_CORE_NDAPI auto IsUniformScale(
  const glm::vec3& scale, float epsilon = 1e-4F) noexcept -> bool;

//! Check for near-identity rotation.
OXGN_CORE_NDAPI auto IsIdentityRotation(
  const glm::quat& rotation, float epsilon = 1e-4F) noexcept -> bool;

} // namespace oxygen::transforms
