//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>

#include <Oxygen/Core/Types/ResolvedView.h>

namespace oxygen {
namespace engine {
  struct ViewContext;
} // namespace engine

// Resolver now accepts a reference to a `ViewContext` so callers (renderer)
// can work directly with the authoritative view data from `FrameContext`.
using ViewResolver = std::function<ResolvedView(const engine::ViewContext&)>;

} // namespace oxygen
