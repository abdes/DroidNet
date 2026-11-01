//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::MemoryFileSystem;
using oxygen::cooker::testing::NodeFlags;
using oxygen::cooker::testing::SceneBuilder;
using oxygen::cooker::testing::ScriptAssetValue;
using oxygen::cooker::testing::ScriptNodeValue;
namespace cooked = oxygen::content::cooked;
namespace data = oxygen::data;
namespace world = oxygen::data::pak::world;
namespace script = oxygen::data::pak::scripting;
using data::schema::OverrideId;

auto Key(const char* path) -> data::AssetKey
{
  return data::AssetKey::FromVirtualPath(path);
}

//! Whether `references` hold the asset `key` of type `type`.
auto HoldsAsset(const data::AssetReferences& references,
  const data::AssetKey& key, const data::AssetType type) -> bool
{
  return std::ranges::any_of(references.Keys(),
    [&key, type](const data::KeyReference& reference) -> bool {
      return reference.kind == data::KeyReferenceKind::kAsset
        && reference.key == key && reference.expected_type == type;
    });
}

//! Nodes keep their names, parents, transforms and flags; their IDs derive
//! from their identity in the scene.
NOLINT_TEST(SceneBuilderTest, NodesReadBackWithTheirIdentity)
{
  auto main = SceneBuilder { "Main" };
  const auto root = main.Node("root");
  const auto crate = main.Node("crate", root);
  main.Place(crate, { .translation = { 1.0F, 2.0F, 3.0F } });
  main.Flags(crate, NodeFlags { .values = world::kSceneNodeFlag_Visible });

  const auto scene = main.Build(Key("/Game/Main.oscene"));

  const auto nodes = scene->GetNodes();
  ASSERT_EQ(nodes.size(), 2U);
  EXPECT_EQ(scene->GetNodeName(nodes[0]), "root");
  EXPECT_EQ(scene->GetNodeName(nodes[1]), "crate");
  EXPECT_EQ(nodes[0].parent_index, 0U);
  EXPECT_EQ(nodes[1].parent_index, 0U);
  EXPECT_EQ(nodes[1].translation[2], 3.0F);
  EXPECT_EQ(nodes[1].node_flags, world::kSceneNodeFlag_Visible);
  EXPECT_EQ(nodes[0].node_id, data::NodeId::FromStableIdentity("/Main/root"));
  EXPECT_EQ(
    main.IdOf(crate), data::NodeId::FromStableIdentity("/Main/root/crate"));
}

//! Components read back sorted by node, and the cook's references of the
//! scene hold their assets.
NOLINT_TEST(SceneBuilderTest, ComponentsReadBackSortedWithReferences)
{
  auto main = SceneBuilder { "Main" };
  const auto root = main.Node("root");
  const auto lamp = main.Node("lamp", root);
  const auto crate = main.Node("crate", root);
  main.Renderable(crate, Key("/Game/Crate.ogeo"));
  main.Renderable(lamp, Key("/Game/Lamp.ogeo"));
  main.PointLight(lamp);
  main.PerspectiveCamera(root);
  main.Override(crate, OverrideId::kGeometrySurfaceMaterial,
    { .as_asset = Key("/Game/Red.omat") },
    data::SurfaceId::FromStableIdentity("rim"),
    oxygen::base::Sha256Digest { 1U });

  const auto scene = main.Build(Key("/Game/Main.oscene"));

  const auto renderables = scene->GetComponents<world::RenderableRecord>();
  ASSERT_EQ(renderables.size(), 2U);
  EXPECT_EQ(renderables[0].node_index, lamp.Index());
  EXPECT_EQ(renderables[1].node_index, crate.Index());
  EXPECT_EQ(scene->GetComponents<world::PointLightRecord>().size(), 1U);
  EXPECT_EQ(scene->GetComponents<world::PerspectiveCameraRecord>().size(), 1U);
  ASSERT_EQ(scene->GetComponents<world::OverrideRecord>().size(), 1U);
  const auto references = main.References();
  EXPECT_TRUE(HoldsAsset(
    references, Key("/Game/Crate.ogeo"), data::AssetType::kGeometry));
  EXPECT_TRUE(
    HoldsAsset(references, Key("/Game/Red.omat"), data::AssetType::kMaterial));
}

//! A slot's parameters of every type read back through the scene's slots,
//! and the cook's references hold its script and asset parameters.
NOLINT_TEST(SceneBuilderTest, ScriptSlotsReadBackWithTheirParameters)
{
  auto main = SceneBuilder { "Main" };
  const auto root = main.Node("root");
  const auto target = main.Node("target", root);
  const auto slot = main.ScriptSlot(root, Key("/Game/Spin.oscript"),
    {
      { .key="on", .value=true },
      { .key="count", .value=3 },
      { .key="speed", .value=1.5F },
      { .key="label", .value=std::string("fast") },
      { .key="axis", .value=std::array { 0.0F, 1.0F, 0.0F } },
      {
        .key="helper",
        .value=ScriptAssetValue {
          .key = Key("/Game/Helper.oscript"),
          .type = data::AssetType::kScript,
        },
      },
      { .key="follow", .value=ScriptNodeValue { .node = target, .path = {} } },
    },
    4);

  const auto scene = main.Build(Key("/Game/Main.oscene"));

  const auto components
    = scene->GetComponents<script::ScriptingComponentRecord>();
  ASSERT_EQ(components.size(), 1U);
  const auto slots = scene->ReadScriptSlots(
    components[0].slot_start_index, components[0].slot_count);
  ASSERT_EQ(slots.size(), 1U);
  EXPECT_EQ(slots[0].slot_id, slot.Id());
  EXPECT_EQ(slots[0].execution_order, 4);
  const auto params = scene->ReadScriptParameters(slots[0]);
  ASSERT_EQ(params.size(), 7U);
  EXPECT_EQ(params[1].type, script::ScriptParamType::kInt32);
  EXPECT_EQ(params[1].value.as_int32, 3);
  EXPECT_EQ(params[3].type, script::ScriptParamType::kString);
  EXPECT_EQ(scene->GetStrings().Get(params[3].value.as_string_offset), "fast");
  EXPECT_EQ(params[4].type, script::ScriptParamType::kVec3);
  EXPECT_EQ(params[6].type, script::ScriptParamType::kNode);
  EXPECT_EQ(params[6].value.as_node.node_index, target.Index());
  const auto references = main.References();
  EXPECT_TRUE(HoldsAsset(
    references, Key("/Game/Spin.oscript"), data::AssetType::kScript));
  EXPECT_TRUE(HoldsAsset(
    references, Key("/Game/Helper.oscript"), data::AssetType::kScript));
}

//! Instances, changes below them and a starting camera an instance brings in
//! name their nodes by path.
NOLINT_TEST(SceneBuilderTest, CompositionNamesInstancedNodesByPath)
{
  auto model = SceneBuilder { "Model" };
  const auto body = model.Node("body");
  const auto eye = model.Node("eye", body);
  model.PerspectiveCamera(eye).ModelScene();
  auto street = SceneBuilder { "Street" };
  const auto root = street.Node("root");
  const auto holder = street.Node("holder", root);
  street.Instance(holder, Key("/Game/Model.oscene"))
    .Change(holder, { model.IdOf(eye) }, OverrideId::kTransformPositionX,
      { .as_float = 2.0F })
    .StartingCamera(holder, { model.IdOf(eye) })
    .StartingInputContext(Key("/Game/Play.oimap"), 2);

  const auto scene = street.Build(Key("/Game/Street.oscene"));

  EXPECT_TRUE(model.Build(Key("/Game/Model.oscene"))->IsModelScene());
  const auto camera = scene->GetStartingCamera();
  ASSERT_HAS_VALUE(camera);
  EXPECT_EQ(camera->node_index, holder.Index());
  EXPECT_EQ(
    scene->GetNodePath(*camera), std::vector<data::NodeId> { model.IdOf(eye) });
  const auto changes = scene->GetComponents<world::OverrideRecord>();
  ASSERT_EQ(changes.size(), 1U);
  EXPECT_EQ(scene->GetNodePath(changes[0].target),
    std::vector<data::NodeId> { model.IdOf(eye) });
  EXPECT_EQ(
    scene->GetComponents<world::StartingInputContextRecord>().size(), 1U);
  EXPECT_TRUE(HoldsAsset(
    street.References(), Key("/Game/Model.oscene"), data::AssetType::kScene));
}

//! Environment records read back after the scene's tables, and a bound
//! texture row is a resource reference of the scene.
NOLINT_TEST(SceneBuilderTest, EnvironmentAndTexturesReadBack)
{
  auto main = SceneBuilder { "Main" };
  static_cast<void>(main.Node("root"));
  auto fog = world::FogEnvironmentRecord {};
  fog.extinction_sigma_t_per_m = 0.05F;
  main.Environment(fog);
  const auto mask = main.Texture(oxygen::ResourceIndexT { 3U });

  const auto scene = main.Build(Key("/Game/Main.oscene"));

  const auto read = scene->TryGetFogEnvironment();
  ASSERT_HAS_VALUE(read);
  EXPECT_EQ(read->extinction_sigma_t_per_m, 0.05F);
  // The references are a value: the span views them only while they live.
  const auto references = main.References();
  const auto resources = references.Resources();
  ASSERT_EQ(resources.size(), 1U);
  EXPECT_EQ(mask.get(), 0U);
  EXPECT_EQ(resources[0].index, oxygen::ResourceIndexT { 3U });
}

//! A handle of another builder, a repeated node, flags a cook never writes,
//! two cameras on a node and a starting camera the reader refuses all throw.
NOLINT_TEST(SceneBuilderTest, RefusesContentNoCookWrites)
{
  auto main = SceneBuilder { "Main" };
  auto other = SceneBuilder { "Other" };
  const auto root = main.Node("root");
  const auto stranger = other.Node("root");

  EXPECT_THROW(
    main.Renderable(stranger, Key("/Game/Crate.ogeo")), std::invalid_argument);
  EXPECT_THROW(static_cast<void>(main.Node("root")), std::invalid_argument);
  EXPECT_THROW(main.Flags(root,
                 NodeFlags {
                   .values = world::kSceneNodeFlag_Visible,
                   .inherited = world::kSceneNodeFlag_Visible,
                 }),
    std::invalid_argument);
  main.PerspectiveCamera(root);
  EXPECT_THROW(main.OrthographicCamera(root), std::invalid_argument);
  auto lonely = SceneBuilder { "Lonely" };
  lonely.StartingCamera(lonely.Node("root"));
  EXPECT_THROW(static_cast<void>(lonely.Bytes()), std::logic_error);
}

//! A built scene in a loose root carries the cook's references in the index.
NOLINT_TEST(SceneBuilderTest, LooseRootRecordsTheSceneReferences)
{
  auto game = LooseCookedRoot { "Game" };
  const auto crate = game.Geometry("Props/Crate.ogeo");
  auto main = SceneBuilder { "Main" };
  main.Renderable(main.Node("root"), crate);
  const auto key = game.Scene("Scenes/Main.oscene", main);

  auto storage = MemoryFileSystem {};
  const auto folder = std::filesystem::path("/cooked/root");
  storage.AddFiles(folder, game.Files());
  const auto content = cooked::CookedContent::Open(storage,
    data::CookedSource {
      .kind = data::CookedSourceKind::kLooseCooked,
      .path = folder,
    });

  const auto* asset = content.FindAsset(key);
  ASSERT_NE(asset, nullptr);
  EXPECT_TRUE(HoldsAsset(asset->references, crate, data::AssetType::kGeometry));
  EXPECT_NO_THROW(data::SceneAsset(key, content.Descriptor(key)));
}

} // namespace
