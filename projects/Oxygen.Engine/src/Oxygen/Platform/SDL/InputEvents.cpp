//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Platform/Platform.h"

#include <SDL3/SDL.h>

using oxygen::platform::InputEvents;

auto InputEvents::ProcessPlatformEvents() -> co::Co<>
{
    co_await co::kSuspendForever;

    (void)1;
}
