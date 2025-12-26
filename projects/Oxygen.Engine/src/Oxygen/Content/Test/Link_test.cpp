//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdlib>

#include <Oxygen/Composition/Object.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/EngineTag.h>
#include <Oxygen/Data/AssetType.h>

class DummyAsset : public oxygen::Object {
  OXYGEN_TYPED(DummyAsset)
};

namespace oxygen::content::internal {

auto EngineTagFactory::Get() noexcept -> EngineTag { return EngineTag {}; }

} // namespace oxygen::content::internal

auto main(int /*argc*/, char** /*argv*/) -> int
{
  using oxygen::content::internal::EngineTagFactory;
  using enum oxygen::data::AssetType;

  oxygen::content::AssetLoader loader(EngineTagFactory::Get());

  return EXIT_SUCCESS;
}
