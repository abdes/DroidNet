//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>

#include <Oxygen/Platform/PlatformEvent.h>
#include <Oxygen/Platform/UiEvent.h>
#include <Oxygen/Platform/api_export.h>

namespace oxygen::platform::sdl {

//! Translates a native SDL event into the backend-independent UI event, or
//! returns nothing when the event is not one a UI layer consumes.
/*!
 A text input event refers to memory owned by the native event, so the result
 must not outlive `event`. Exported only so that the backend tests can reach
 it; the platform is its sole caller.
*/
OXGN_PLAT_NDAPI auto TranslateUiEvent(const PlatformEvent& event)
  -> std::optional<UiEvent>;

} // namespace oxygen::platform::sdl
