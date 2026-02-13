//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdlib>
#include <exception>
#include <iostream>

#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>

auto main(int /*argc*/, char** /*argv*/) noexcept -> int
{
  try {
    // Get the singleton instance of the GraphicsBackendLoader
    auto& loader = oxygen::GraphicsBackendLoader::GetInstance();

    // Load the backend using the singleton
    auto backend
      = loader.LoadBackend(
        oxygen::graphics::BackendType::kDirect3D12, {}, {});

    if (!backend.expired()) {
      std::cout << "Successfully loaded the graphics backend\n";
      return EXIT_SUCCESS;
    }
    std::cerr << "Failed to load the graphics backend\n";
  } catch (const std::exception& ex) {
    try {
      std::cerr << "Exception caught: " << ex.what() << "\n";
    } catch (...) {
      // Cannot do anything if ex.what() throws
      (void)0;
    }
  } catch (...) {
    // Catch any other exceptions
    (void)0;
  }
  return EXIT_FAILURE;
}
