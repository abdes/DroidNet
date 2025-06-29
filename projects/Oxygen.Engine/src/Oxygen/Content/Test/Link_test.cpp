//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>

#include <Oxygen/Content/AssetLoader.h>

class DummyAsset : public oxygen::Object {
  OXYGEN_TYPED(DummyAsset)
};

auto main(int /*argc*/, char** /*argv*/) -> int
{
  using oxygen::content::AssetLoader;
  using enum oxygen::content::AssetType;

  AssetLoader loader;
  loader.RegisterLoader(kGeometry,
    [](const auto& /*pak*/,
      const auto& /*entry*/) -> std::unique_ptr<DummyAsset> {
      return nullptr; // Dummy loader
    });

  return EXIT_SUCCESS;
}
