//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

//! \file
//! \brief Constants and types for the graphics special resources.
/*!
 These resources are not managed by the backend graphics API, but are
 managed by the engine and treated as resources with a handle.
 */

#include "Oxygen/Base/ResourceHandle.h"

namespace oxygen::graphics::resources {

constexpr ResourceHandle::ResourceTypeT kWindow = 1;
constexpr ResourceHandle::ResourceTypeT kSurface = 2;

using WindowId = ResourceHandle;
using SurfaceId = ResourceHandle;

} // namespace oxygen::graphics::resources
