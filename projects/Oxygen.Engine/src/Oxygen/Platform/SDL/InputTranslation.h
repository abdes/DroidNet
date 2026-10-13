//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_stdinc.h>

#include <Oxygen/Platform/Input.h>

namespace oxygen::platform::sdl {

//! Maps an SDL key code to Oxygen's key vocabulary; `Key::kNone` when the key
//! has no Oxygen equivalent.
[[nodiscard]] auto MapKeyCode(SDL_Keycode code) -> Key;

//! Maps an SDL mouse button to Oxygen's; `MouseButton::kNone` when it has no
//! Oxygen equivalent.
[[nodiscard]] auto MapMouseButton(Uint8 button) -> MouseButton;

} // namespace oxygen::platform::sdl
