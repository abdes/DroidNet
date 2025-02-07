//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <SDL3/SDL.h>

#include "Oxygen/Platform/Platform.h"
#include "Oxygen/Platform/SDL/Wrapper.h"

using oxygen::Platform;

Platform::Platform()
{
    Compose();

    platform::sdl::Init(SDL_INIT_VIDEO);
    platform::sdl::SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");

    LOG_F(INFO, "Platform/SDL3 initialized");
}

Platform::~Platform()
{
    LOG_F(INFO, "Platform/SDL3 destroyed");

    // ->Final<- thing to do is to terminate SDL3.
    platform::sdl::Terminate();
}
