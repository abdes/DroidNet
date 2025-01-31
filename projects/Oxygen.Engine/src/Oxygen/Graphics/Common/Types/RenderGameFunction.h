//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>

#include "Oxygen/Graphics/Common/Forward.h"

namespace oxygen::graphics {

using RenderGameFunction = std::function<CommandLists(const RenderTarget& render_target)>;

} // namespace oxygen::graphics
