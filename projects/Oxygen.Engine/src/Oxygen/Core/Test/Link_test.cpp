//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>

#include <Oxygen/Core/Version.h>

auto main(int /*argc*/, char** /*argv*/) -> int
{
    std::cout << "Oxygen v" << static_cast<int>(oxygen::version::Major())
              << "." << static_cast<int>(oxygen::version::Minor())
              << "." << static_cast<int>(oxygen::version::Patch()) << '\n';

    return 0;
}
