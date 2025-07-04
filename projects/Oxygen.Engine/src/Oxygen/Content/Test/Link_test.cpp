//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/LoaderFunctions.h>

class DummyAsset : public oxygen::Object {
  OXYGEN_TYPED(DummyAsset)
};

template <oxygen::serio::Stream S>
auto DummyLoader(oxygen::content::LoaderContext<S> /*context*/)
  -> std::unique_ptr<DummyAsset>
{
  return std::make_unique<DummyAsset>();
}

static_assert(oxygen::content::LoadFunction<
  decltype(DummyLoader<oxygen::serio::FileStream<>>)>);

auto main(int /*argc*/, char** /*argv*/) -> int
{
  using oxygen::content::AssetLoader;
  using enum oxygen::data::AssetType;

  AssetLoader loader;
  loader.RegisterLoader(DummyLoader<oxygen::serio::FileStream<>>);

  return EXIT_SUCCESS;
}
