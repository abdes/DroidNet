//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/test/Support/TestValues.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormatSerioWriters.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>

//! Scene roots shared by the plan-builder, patch and repack tests.
namespace oxygen::content::pak::test {

using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::SceneBuilder;
using oxygen::cooker::testing::SeedBytes;

//=== Scenes that bind scripts ===--------------------------------------------//

//! The key of script `index` of the scene of `seed`.
[[nodiscard]] inline auto ScriptKey(const uint8_t seed, const uint8_t index)
  -> data::AssetKey
{
  return cooker::test::MakeAssetKey(static_cast<uint8_t>((seed * 10U) + index));
}

//! The parameter value of script `index` of the scene of `seed`.
[[nodiscard]] constexpr auto ScriptParameter(
  const uint8_t seed, const uint32_t index) -> int32_t
{
  return (seed * 100) + static_cast<int32_t>(index);
}

//! A scene of two nodes, each with one scripting slot that binds
//! `ScriptKey(seed, i)` with the int parameter `v`, `ScriptParameter(seed, i)`.
[[nodiscard]] inline auto BuildScriptsScene(const uint8_t seed) -> SceneBuilder
{
  auto scene = SceneBuilder { "Scene" + std::to_string(seed) };
  for (uint8_t i = 0U; i < 2U; ++i) {
    scene.ScriptSlot(scene.Node("node" + std::to_string(i)), ScriptKey(seed, i),
      { { .key = "v", .value = ScriptParameter(seed, i) } });
  }
  return scene;
}

//! A loose root holding the scene of `seed` and the two scripts its slots
//! bind.
struct SceneScripts final {
  LooseCookedRoot root;
  data::AssetKey scene;
};

[[nodiscard]] inline auto MakeSceneScripts(const uint8_t seed) -> SceneScripts
{
  auto root = LooseCookedRoot { "Game", seed };
  for (uint8_t i = 0U; i < 2U; ++i) {
    auto script = data::pak::scripting::ScriptAssetDesc {};
    script.header.asset_type = static_cast<uint8_t>(data::AssetType::kScript);
    script.header.version = data::pak::scripting::kScriptAssetVersion;
    root.Add({
      .type = data::AssetType::kScript,
      .leaf
      = "Logic" + std::to_string(seed) + "-" + std::to_string(i) + ".oscript",
      .descriptor = data::MakeDescriptorBytes(script),
      .key = ScriptKey(seed, i),
    });
  }
  const auto scene = root.Scene(
    "Scene" + std::to_string(seed) + ".oscene", BuildScriptsScene(seed));
  return { .root = std::move(root), .scene = scene };
}

//=== Scenes that bind textures through a mask ===----------------------------//

//! A loose root holding a scene whose post-process metering mask binds the
//! root's one texture, a material whose base color binds it too, and the
//! scene's physics sidecar. Merging two of them has to remap the second
//! root's texture.
struct SceneMask final {
  LooseCookedRoot root;
  data::AssetKey scene;
  data::AssetKey material;
  data::AssetKey sidecar;
  //! The texture's payload: `SeedBytes(seed)`.
  std::vector<std::byte> texture_payload;
  //! The index the scene's metering mask names the texture by.
  data::ResourceReferenceIndex mask;
};

[[nodiscard]] inline auto MakeSceneMask(const uint8_t seed) -> SceneMask
{
  auto out = SceneMask {
    .root = LooseCookedRoot { "Game", seed },
    .scene = {},
    .material = {},
    .sidecar = {},
    .texture_payload = SeedBytes(seed),
    .mask = data::kNoResourceReference,
  };
  const auto texture = out.root.AddTexture(out.texture_payload);

  auto scene = SceneBuilder { "Scene" + std::to_string(seed) };
  out.mask = scene.Texture(texture);
  auto post = data::pak::world::PostProcessVolumeEnvironmentRecord {};
  post.auto_exposure_metering_mask = out.mask;
  scene.Environment(post);
  out.scene = out.root.Scene("Scene" + std::to_string(seed) + ".oscene", scene);
  out.material
    = out.root.Material("Material" + std::to_string(seed) + ".omat", texture);
  out.sidecar = out.root.PhysicsSidecar(
    "Physics" + std::to_string(seed) + ".opscene", out.scene);
  return out;
}

} // namespace oxygen::content::pak::test
