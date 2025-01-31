//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>

#include "Oxygen/Core/Engine.h"

using oxygen::Engine;

auto main(int /*argc*/, char** /*argv*/) -> int
{
    const Engine::Properties props {};
    Engine engine({}, {}, props);
    std::cout << Engine::Name() << " " << Engine::Version() << '\n';

    return 0;
}
