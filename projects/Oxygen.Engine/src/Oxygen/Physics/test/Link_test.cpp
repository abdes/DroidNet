//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdlib>
#include <iostream>

#include <Oxygen/Physics/Physics.h>

auto main(int /*argc*/, char** /*argv*/) -> int
{
  const auto backend = oxygen::physics::GetBackendName();
  if (backend.empty()) {
    std::cerr << "physics backend name is empty\n";
    return EXIT_FAILURE;
  }
  std::cout << "physics backend: " << backend << "\n";
  return EXIT_SUCCESS;
}
