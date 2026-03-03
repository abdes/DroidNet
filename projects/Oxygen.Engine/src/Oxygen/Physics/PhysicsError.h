//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Physics/api_export.h>

namespace oxygen::physics {

enum class PhysicsError : uint8_t {
  kInvalidArgument = 0,
  kWorldNotFound,
  kBodyNotFound,
  kCharacterNotFound,
  kInvalidCollisionMask,
  kBufferTooSmall,
  kAlreadyExists,
  kNotInitialized,
  kBackendInitFailed,
  kNotImplemented,
  kShapeCompoundZeroChildren,
  kShapeCompoundChildScaleContractViolation,
  kBackendUnavailable,
  kResourceExhausted,
};

OXGN_PHYS_NDAPI auto to_string(PhysicsError value) noexcept -> std::string_view;

template <typename T> using PhysicsResult = Result<T, PhysicsError>;

} // namespace oxygen::physics
