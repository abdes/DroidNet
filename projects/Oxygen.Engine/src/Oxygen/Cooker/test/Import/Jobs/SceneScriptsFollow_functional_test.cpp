//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/SceneScripts.cpp (a re-cooked scene keeps its
// scripts), Import/Internal/Jobs/SceneDescriptorImportJob.cpp,
// Import/Internal/WorkDispatcher.cpp

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ScriptImportTestSupport.h"
#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/SceneSource.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  namespace script = data::pak::scripting;

  constexpr auto kRoot
    = std::string_view { "00000000-0000-4000-8000-000000000001" };
  constexpr auto kDoor
    = std::string_view { "00000000-0000-4000-8000-000000000002" };
  constexpr auto kLamp
    = std::string_view { "00000000-0000-4000-8000-000000000003" };
  constexpr auto kPartNode
    = std::string_view { "00000000-0000-4000-8000-0000000000a1" };
  constexpr auto kDoorSpeed = 3.5F;

  //! The script of the door: a float `speed` and the node `target`.
  constexpr auto kDoorScript = std::string_view {
    "local Script = oxygen.script {\n"
    "  params = {\n"
    "    speed = oxygen.param.float {},\n"
    "    target = oxygen.param.node {},\n"
    "  },\n"
    "}\n"
    "return Script\n",
  };

  //! A node of a scene: its ID and name; the first node is the root of the
  //! others.
  struct NodeSpec final {
    std::string_view id;
    std::string_view name;
  };

  //! The script parameter `key` of a slot, if the slot sets it.
  auto FindParam(const data::SceneAsset& scene,
    const script::ScriptSlotRecord& slot, const std::string_view key)
    -> std::optional<script::ScriptParamRecord>
  {
    for (const auto& record : scene.ReadScriptParameters(slot)) {
      if (data::TryGetScriptParamKey(record, scene.GetStrings()) == key) {
        return record;
      }
    }
    return std::nullopt;
  }

  //! A cooked root holding the scene `Hall`, whose door runs a script naming
  //! its lamp, given by a scripting sidecar.
  class SceneScriptsFollowTest : public ScriptingImportTestBase {
  protected:
    void SetUp() override
    {
      ScriptingImportTestBase::SetUp();
      root_ = temp_.Path() / "follow";
    }

    //! Cooks the scene `name` with `nodes`; `extra` adds members to a node,
    //! by index.
    auto CookScene(const std::string_view name,
      const std::vector<NodeSpec>& nodes,
      const std::vector<std::pair<size_t, nlohmann::json>>& extra = {})
      -> ImportReport
    {
      auto node_list = nlohmann::json::array();
      for (size_t index = 0; index < nodes.size(); ++index) {
        auto node = nlohmann::json {
          { "id", nodes.at(index).id },
          { "name", nodes.at(index).name },
        };
        if (index != 0U) {
          node["parent"] = 0;
        }
        for (const auto& [at, members] : extra) {
          if (at == index) {
            node.update(members);
          }
        }
        node_list.push_back(std::move(node));
      }
      auto request = ImportRequest {};
      request.source_path
        = root_ / "input" / (std::string(name) + ".oscene.json");
      request.asset_virtual_path = CanonicalSceneVirtualPath(name);
      request.source_mount_path
        = oxygen::cooker::test::SourceMountPath(request.source_path);
      request.cooked_root = root_;
      request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
        .descriptor_json = nlohmann::json {
          { "schema_version", internal::kSceneDescriptorSchemaVersion },
          { "name", name },
          { "nodes", std::move(node_list) },
        }
                             .dump(),
      };
      return Submit(std::move(request));
    }

    auto CookHall(const std::vector<NodeSpec>& nodes) -> ImportReport
    {
      return CookScene("Hall", nodes);
    }

    //! Cooks the door's script.
    auto CookScript() -> bool
    {
      const auto source = root_ / "input" / "door.luau";
      WriteText(source, kDoorScript);
      return Submit(MakeScriptRequest({
                      .source_path = source,
                      .cooked_root = root_,
                    }))
        .success;
    }

    //! Applies to `scene` a sidecar whose rows are `bindings`.
    auto ApplySidecar(const std::string_view scene,
      const nlohmann::json& bindings,
      const nlohmann::json& instance_bindings = nlohmann::json::array())
      -> ImportReport
    {
      const auto source
        = root_ / "input" / (std::string(scene) + ".sidescript.json");
      WriteText(source,
        nlohmann::json {
          { "bindings", bindings },
          { "instance_bindings", instance_bindings },
        }
          .dump(2));
      return Submit(MakeSidecarRequest({
        .source_path = source,
        .cooked_root = root_,
        .scene_virtual_path = CanonicalSceneVirtualPath(scene),
      }));
    }

    //! The binding of the door's script on node `door`, naming node `lamp`.
    [[nodiscard]] static auto DoorBinding(
      const uint32_t door, const uint32_t lamp) -> nlohmann::json
    {
      return nlohmann::json::array({
        {
          { "node_index", door },
          { "slot_id", kMainSlotId },
          {
            "script_virtual_path",
            SpellReference(CanonicalSceneVirtualPath("Hall"),
              CanonicalScriptVirtualPath("door")),
          },
          {
            "params",
            nlohmann::json::array({
              {
                { "key", "speed" },
                { "type", "float" },
                { "value", kDoorSpeed },
              },
              {
                { "key", "target" },
                { "type", "node" },
                { "value", { { "node", lamp } } },
              },
            }),
          },
        },
      });
    }

    //! Cooks `Hall` with a root, the door and the lamp, then scripts the door.
    auto CookScriptedHall() -> void
    {
      ASSERT_TRUE(CookScript());
      ASSERT_TRUE(
        CookHall({ { kRoot, "Root" }, { kDoor, "Door" }, { kLamp, "Lamp" } })
          .success);
      const auto scripted = ApplySidecar("Hall", DoorBinding(1, 2));
      ASSERT_TRUE(scripted.success);
    }

    //! The cooked scene `name`, loaded and validated.
    [[nodiscard]] auto Load(const std::string_view name) const
      -> data::SceneAsset
    {
      const auto key
        = data::AssetKey::FromVirtualPath(CanonicalSceneVirtualPath(name));
      return { key, ReadCurrentDescriptor(root_, key) };
    }

    ScopedTempDir temp_;
    std::filesystem::path root_;
  };

  //! A scene re-cooked with the same nodes keeps the scripts its sidecar
  //! gave it.
  NOLINT_TEST_F(SceneScriptsFollowTest, SameNodesKeepTheirScripts)
  {
    CookScriptedHall();

    const auto report = CookHall({
      { .id = kRoot, .name = "Root" },
      { .id = kDoor, .name = "Door" },
      { .id = kLamp, .name = "Lamp" },
    });

    ASSERT_TRUE(report.success);
    const auto hall = Load("Hall");
    const auto components
      = hall.GetComponents<script::ScriptingComponentRecord>();
    ASSERT_EQ(components.size(), 1U);
    EXPECT_EQ(components.front().node_index, 1U);
    const auto slots = hall.ReadScriptSlots(0U, components.front().slot_count);
    ASSERT_EQ(slots.size(), 1U);
    const auto speed = FindParam(hall, slots.front(), "speed");
    ASSERT_HAS_VALUE(speed);
    EXPECT_EQ(speed->value.as_float, kDoorSpeed);
    const auto target = FindParam(hall, slots.front(), "target");
    ASSERT_HAS_VALUE(target);
    EXPECT_EQ(target->value.as_node.node_index, 2U);
  }

  //! Scripts follow their nodes, and node values the nodes they name, when
  //! the re-cooked scene orders its nodes otherwise.
  NOLINT_TEST_F(SceneScriptsFollowTest, MovedNodesKeepTheirScripts)
  {
    CookScriptedHall();

    const auto report = CookHall({
      { .id = kRoot, .name = "Root" },
      { .id = kLamp, .name = "Lamp" },
      { .id = kDoor, .name = "Door" },
    });

    ASSERT_TRUE(report.success);
    const auto hall = Load("Hall");
    const auto components
      = hall.GetComponents<script::ScriptingComponentRecord>();
    ASSERT_EQ(components.size(), 1U);
    EXPECT_EQ(components.front().node_index, 2U);
    const auto slots = hall.ReadScriptSlots(0U, components.front().slot_count);
    ASSERT_EQ(slots.size(), 1U);
    const auto target = FindParam(hall, slots.front(), "target");
    ASSERT_HAS_VALUE(target);
    EXPECT_EQ(target->value.as_node.node_index, 1U);
  }

  //! A node value naming a node the re-cooked scene no longer holds is unset,
  //! and the scripts keep their other values.
  NOLINT_TEST_F(SceneScriptsFollowTest, RemovedTargetUnsetsTheNodeValue)
  {
    CookScriptedHall();

    const auto report = CookHall(
      { { .id = kRoot, .name = "Root" }, { .id = kDoor, .name = "Door" } });

    ASSERT_TRUE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, import_codes::kSceneScriptsNodeValueMissing));
    const auto hall = Load("Hall");
    const auto components
      = hall.GetComponents<script::ScriptingComponentRecord>();
    ASSERT_EQ(components.size(), 1U);
    const auto slots = hall.ReadScriptSlots(0U, components.front().slot_count);
    ASSERT_EQ(slots.size(), 1U);
    const auto target = FindParam(hall, slots.front(), "target");
    ASSERT_HAS_VALUE(target);
    EXPECT_EQ(target->value.as_node.node_index, script::kUnsetNodeParam);
    const auto speed = FindParam(hall, slots.front(), "speed");
    ASSERT_HAS_VALUE(speed);
    EXPECT_EQ(speed->value.as_float, kDoorSpeed);
  }

  //! The scripts of a node the re-cooked scene no longer holds are dropped
  //! with a warning; the scene still cooks.
  NOLINT_TEST_F(SceneScriptsFollowTest, RemovedNodeDropsItsScripts)
  {
    CookScriptedHall();

    const auto report = CookHall(
      { { .id = kRoot, .name = "Root" }, { .id = kLamp, .name = "Lamp" } });

    ASSERT_TRUE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, import_codes::kSceneScriptsNodeMissing));
    const auto hall = Load("Hall");
    EXPECT_TRUE(hall.GetComponents<script::ScriptingComponentRecord>().empty());
  }

  //! A sidecar applied again after its scene was re-cooked replaces the
  //! scripts the scene kept, without doubling them.
  NOLINT_TEST_F(SceneScriptsFollowTest, SidecarAgainReplacesKeptScripts)
  {
    CookScriptedHall();
    ASSERT_TRUE(
      CookHall({ { kRoot, "Root" }, { kDoor, "Door" }, { kLamp, "Lamp" } })
        .success);

    const auto report = ApplySidecar("Hall", DoorBinding(1, 2));

    ASSERT_TRUE(report.success);
    const auto hall = Load("Hall");
    const auto components
      = hall.GetComponents<script::ScriptingComponentRecord>();
    ASSERT_EQ(components.size(), 1U);
    EXPECT_EQ(components.front().slot_count, 1U);
  }

  //! Changes of an instanced node's script parameters follow their instance
  //! root, and are dropped with a warning when the scene no longer holds it.
  NOLINT_TEST_F(SceneScriptsFollowTest, InstanceChangesFollowTheirNode)
  {
    ASSERT_TRUE(CookScript());
    ASSERT_TRUE(CookScene("Part", { { kPartNode, "Part" } }).success);
    ASSERT_TRUE(ApplySidecar("Part",
      nlohmann::json::array({ {
        { "node_index", 0 },
        { "slot_id", kMainSlotId },
        { "script_virtual_path",
          SpellReference(CanonicalSceneVirtualPath("Part"),
            CanonicalScriptVirtualPath("door")) },
      } }))
        .success);
    const auto instance = nlohmann::json {
      {
        "instance",
        {
          {
            "scene_ref",
            SpellReference(CanonicalSceneVirtualPath("Level"),
              CanonicalSceneVirtualPath("Part")),
          },
        },
      },
    };
    ASSERT_TRUE(CookScene(
      "Level", { { kRoot, "Root" }, { kDoor, "Part" } }, { { 1U, instance } })
        .success);
    ASSERT_TRUE(ApplySidecar("Level", nlohmann::json::array(),
      nlohmann::json::array({ {
        { "node_index", 1 },
        { "node_path", nlohmann::json::array({ kPartNode }) },
        { "slot_id", kMainSlotId },
        { "params",
          nlohmann::json::array({ { { "key", "speed" }, { "type", "float" },
            { "value", kDoorSpeed } } }) },
      } }))
        .success);

    const auto moved = CookScene("Level",
      {
        { .id = kRoot, .name = "Root" },
        { .id = kLamp, .name = "Lamp" },
        { .id = kDoor, .name = "Part" },
      },
      { { 2U, instance } });
    const auto moved_level = Load("Level");
    const auto removed
      = CookScene("Level", { { .id = kRoot, .name = "Root" } });
    const auto removed_level = Load("Level");

    ASSERT_TRUE(moved.success);
    const auto changes
      = moved_level.GetComponents<script::ScriptParamOverrideRecord>();
    ASSERT_EQ(changes.size(), 1U);
    EXPECT_EQ(changes.front().target.node_index, 2U);
    const auto part_node = data::NodeId::FromString(kPartNode);
    ASSERT_HAS_VALUE(part_node);
    EXPECT_THAT(moved_level.GetNodePath(changes.front().target),
      ::testing::ElementsAre(*part_node));
    ASSERT_TRUE(removed.success);
    EXPECT_TRUE(HasDiagnosticCode(
      removed.diagnostics, import_codes::kSceneScriptsChangeNodeMissing));
    EXPECT_TRUE(
      removed_level.GetComponents<script::ScriptParamOverrideRecord>().empty());
  }

  //! A model scene imported again keeps the scripts its sidecar gave it.
  NOLINT_TEST_F(SceneScriptsFollowTest, ModelSceneKeepsItsScripts)
  {
    const auto cooked = CookSceneWithScript({
      .cooked_root = root_,
      .script_text = SpeedScriptSource("model"),
    });
    ASSERT_HAS_VALUE(cooked.scene);
    ASSERT_HAS_VALUE(cooked.script);
    const auto sidecar = root_ / "input" / "model.sidescript.json";
    WriteText(sidecar,
      MakeSidecarPayload(MakeSpeedBinding(SpellReference(
        cooked.scene->virtual_path, cooked.script->virtual_path))));
    ASSERT_TRUE(Submit(MakeSidecarRequest({
                         .source_path = sidecar,
                         .cooked_root = root_,
                         .scene_virtual_path = cooked.scene->virtual_path,
                       }))
        .success);

    const auto report = Submit(MakeSceneRequest({
      .source_path = ModelPath(),
      .cooked_root = root_,
    }));

    ASSERT_TRUE(report.success);
    const auto state = ReadSceneScriptState(root_, *cooked.scene);
    ASSERT_EQ(state.slots.size(), 1U);
    EXPECT_EQ(state.slots.front().script_asset_key, cooked.script->key);
    ASSERT_EQ(state.params.size(), 1U);
  }

} // namespace

} // namespace oxygen::content::import::test
