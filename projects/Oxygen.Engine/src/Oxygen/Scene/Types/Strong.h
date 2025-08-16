//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/NamedType.h>

namespace oxygen::scene {

//! Normalized distance in [0,1] used for LOD and geometric metrics.
/*!
 Strong type wrapping a float. Default-initialized to 0.0f. Supports
 arithmetic and comparison operations and implicit function-callable style
 access to the underlying float where convenient.
*/
using NormalizedDistance
  = oxygen::NamedType<float, struct NormalizedDistanceTag, oxygen::Arithmetic,
    oxygen::FunctionCallable, oxygen::DefaultInitialized>;

//! Screen-space error metric for LOD decisions (pixels).
/*!
 Strong type wrapping a float. Default-initialized to 0.0f. Supports
 arithmetic and comparison operations and implicit function-callable style
 access to the underlying float where convenient.
*/
using ScreenSpaceError = oxygen::NamedType<float, struct ScreenSpaceErrorTag,
  oxygen::Arithmetic, oxygen::FunctionCallable, oxygen::DefaultInitialized>;

} // namespace oxygen::scene
