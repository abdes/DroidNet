//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::PakWorkspace;
using oxygen::cooker::testing::SeedBytes;
using oxygen::serio::testing::BytesEqual;
namespace data = oxygen::data;

//! A root of one material, the smallest content a pak can hold.
auto OneMaterial(const std::string& mount = "Game") -> LooseCookedRoot
{
  auto root = LooseCookedRoot { mount };
  static_cast<void>(root.Material("Materials/Wood.omat"));
  return root;
}

//! One request built twice gives the same bytes twice.
NOLINT_TEST(PakWorkspaceTest, RequestsAreDeterministic)
{
  auto workspace = PakWorkspace {};
  const auto request
    = workspace.FullRequest({ workspace.Add(OneMaterial()) }, "game.pak");

  const auto first = workspace.PackOrThrow(request);
  const auto first_bytes
    = std::vector<std::byte>(first.Bytes().begin(), first.Bytes().end());
  const auto second = workspace.PackOrThrow(request);

  EXPECT_THAT(second.Bytes(), BytesEqual(first_bytes));
}

//! Each request of a workspace has an identity of its own.
NOLINT_TEST(PakWorkspaceTest, RequestsHaveDistinctIdentities)
{
  auto workspace = PakWorkspace {};
  const auto source = workspace.Add(OneMaterial());

  const auto first = workspace.FullRequest({ source });
  const auto second = workspace.FullRequest({ source });

  EXPECT_NE(first.source_key, second.source_key);
}

//! A patch request publishes its manifest beside its pak.
NOLINT_TEST(PakWorkspaceTest, PatchRequestsPublishAManifest)
{
  auto workspace = PakWorkspace {};
  const auto game = OneMaterial();

  const auto request = workspace.PatchRequest(
    { workspace.Add(game) }, { game.BaseCatalog() }, "patch.pak");

  EXPECT_EQ(
    request.output_manifest_path, PakWorkspace::PakPath("patch.manifest.json"));
}

//! A failed build throws with every diagnostic, and publishes no pak.
NOLINT_TEST(PakWorkspaceTest, PackOrThrowNamesTheDiagnostics)
{
  auto workspace = PakWorkspace {};
  const auto request
    = workspace.FullRequest({ workspace.Add(LooseCookedRoot { "Game" }) });

  try {
    static_cast<void>(workspace.PackOrThrow(request));
    FAIL() << "an empty pak packed";
  } catch (const std::runtime_error& error) {
    EXPECT_THAT(error.what(), ::testing::HasSubstr("pak.plan."));
  }
  EXPECT_FALSE(workspace.Exists(request.output_pak_path));
}

//! A built pak as a dependency carries its catalog and the textures its browse
//! index names.
NOLINT_TEST(PakWorkspaceTest, DependencyReadsTheBuiltPak)
{
  auto workspace = PakWorkspace {};
  auto engine = OneMaterial("Engine");
  const auto grid = engine.AddTexture(SeedBytes(2U));
  const auto path = engine.DescribeTexture("Textures/Grid.otex", grid);
  const auto request
    = workspace.FullRequest({ workspace.Add(engine) }, "engine.pak");
  static_cast<void>(workspace.PackOrThrow(request));

  const auto dependency = workspace.Dependency(request.output_pak_path);

  EXPECT_EQ(dependency.catalog.mount, "Engine");
  EXPECT_TRUE(
    dependency.texture_keys.contains(data::TextureKeyFromVirtualPath(path)));
}

} // namespace
