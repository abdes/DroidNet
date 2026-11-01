//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <utility>

#include "DemoShell/test/ContentFixtures.h"

#include <Oxygen/Cooker/testing/CookedPak.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::examples::testing {

auto WriteShowcaseRoot(const std::filesystem::path& folder) -> ShowcaseRoot
{
  constexpr auto kBottle = "Scenes/BottleOnBox/BottleOnBox.oscene";
  constexpr auto kLantern = "Scenes/Lantern/Lantern.oscene";
  auto showcase = cooker::testing::LooseCookedRoot { "Showcase" };
  auto root = ShowcaseRoot {
    .root = folder,
    .bottle = showcase.Scene(kBottle),
    .bottle_path = showcase.VirtualPath(kBottle),
    .lantern = showcase.Scene(kLantern),
    .lantern_path = showcase.VirtualPath(kLantern),
  };
  cooker::testing::Materialize(showcase.Files(), folder);
  return root;
}

auto WriteDependentPak(const std::filesystem::path& folder) -> DependentPak
{
  auto base = cooker::testing::LooseCookedRoot { "Base" };
  const auto required = base.Material("Default.omat");
  const auto base_pak = cooker::testing::CookedPak { std::move(base) };

  auto props = cooker::testing::LooseCookedRoot { "Props", 2U };
  props.Add({
    .type = data::AssetType::kMaterial,
    .leaf = "Wood.omat",
    .descriptor = cooker::testing::MaterialDescriptor("Wood").bytes,
    .references = data::AssetReferences::Create({},
      {
        {
          .key = required,
          .kind = data::KeyReferenceKind::kAsset,
          .expected_type = data::AssetType::kMaterial,
        },
      })
      .value(),
  });
  auto props_pak = cooker::testing::CookedPak { std::move(props) };
  props_pak.DependsOn(base_pak);

  auto pak = DependentPak {
    .pak = folder / "props.pak",
    .base_pak = folder / "base.pak",
    .required = required,
  };
  std::filesystem::create_directories(folder);
  cooker::testing::Materialize(props_pak.Encode(), pak.pak);
  cooker::testing::Materialize(base_pak.Encode(), pak.base_pak);
  return pak;
}

} // namespace oxygen::examples::testing
