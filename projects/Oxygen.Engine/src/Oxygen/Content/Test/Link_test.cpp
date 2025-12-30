//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdlib>

#include <tuple>

#include <Oxygen/Composition/Object.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/EngineTag.h>
#include <Oxygen/Content/ResourceTypeList.h>
#include <Oxygen/Data/AssetType.h>

class DummyAsset : public oxygen::Object {
  OXYGEN_TYPED(DummyAsset)
};

namespace oxygen::content::internal {

auto EngineTagFactory::Get() noexcept -> EngineTag { return EngineTag {}; }

} // namespace oxygen::content::internal

namespace {

template <typename... Ts>
auto TouchLoadResourceAsyncInstantiations(oxygen::TypeList<Ts...>) -> void
{
  // Taking the address of each specialization forces the symbol to exist.
  // This intentionally fails at link-time if any `LoadResourceAsync<T>`
  // specialization is declared but not explicitly instantiated/exported.
  [[maybe_unused]] const auto fns = std::tuple {
    static_cast<oxygen::co::Co<std::shared_ptr<Ts>> (
      oxygen::content::AssetLoader::*)(oxygen::content::ResourceKey)>(
      &oxygen::content::AssetLoader::LoadResourceAsync<Ts>)...,
  };
}

} // namespace

auto main(int /*argc*/, char** /*argv*/) -> int
{
  using oxygen::content::internal::EngineTagFactory;
  using enum oxygen::data::AssetType;

  oxygen::content::AssetLoader loader(EngineTagFactory::Get());

  TouchLoadResourceAsyncInstantiations(oxygen::content::ResourceTypeList {});

  return EXIT_SUCCESS;
}
