//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "SceneMetadata.h"
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Cooker/Import/Internal/GeometryDefaults.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneChangeValues.h>
#include <Oxygen/Data/SceneComposition.h>
#include <Oxygen/Data/Schema/Generated.SceneChanges.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::inspection {
namespace {

  using nlohmann::json;
  namespace world = data::pak::world;
  using data::schema::ComponentId;
  using data::schema::OverrideProperty;
  using data::schema::SchemaValue;

  //! Reads and parses the scene descriptor `entry` names.
  auto LoadScene(
    const std::filesystem::path& root, const lc::Inspection::AssetEntry& entry)
    -> std::shared_ptr<const data::SceneAsset>
  {
    const auto relative
      = std::filesystem::path(entry.descriptor_relpath).lexically_normal();
    if (relative.empty() || relative.has_root_path()
      || *relative.begin() == "..") {
      throw std::runtime_error(
        "Descriptor path must stay within its cooked root.");
    }
    serio::FileStream<> stream(root / relative, std::ios::in);
    const auto size = stream.Size();
    if (!size || *size != entry.descriptor_size) {
      throw std::runtime_error(
        "Scene descriptor size does not match its index entry.");
    }
    serio::Reader reader(stream);
    const LoaderContext context {
      .current_asset_key = entry.key,
      .desc_reader = &reader,
      .work_offline = true,
      .parse_only = true,
    };
    return loaders::LoadSceneAsset(context);
  }

  //! Serves the root's scenes to composition, each parsed once; a scene that
  //! is not in the root or does not parse is missing.
  class RootScenes {
  public:
    RootScenes(std::filesystem::path root, const lc::Inspection& inspection)
      : root_(std::move(root))
    {
      for (const auto& entry : inspection.Assets()) {
        if (entry.asset_type == static_cast<uint8_t>(data::AssetType::kScene)) {
          entries_.emplace(entry.key, make_observer(&entry));
        }
      }
    }

    //! The scene `key` names, or null when the root holds none; throws when
    //! it does not parse.
    auto Load(const data::AssetKey& key)
      -> std::shared_ptr<const data::SceneAsset>
    {
      if (const auto found = scenes_.find(key); found != scenes_.end()) {
        return found->second;
      }
      const auto entry = entries_.find(key);
      if (entry == entries_.end()) {
        return nullptr;
      }
      auto scene = LoadScene(root_, *entry->second);
      scenes_.emplace(key, scene);
      return scene;
    }

    auto Resolver() -> data::SceneResolver
    {
      return [this](const data::AssetKey& key)
               -> std::shared_ptr<const data::SceneAsset> {
        try {
          return Load(key);
        } catch (const std::exception&) {
          return nullptr;
        }
      };
    }

    //! The virtual path of the scene `key` names, if the root holds it.
    [[nodiscard]] auto VirtualPath(const data::AssetKey& key) const -> json
    {
      const auto entry = entries_.find(key);
      return entry == entries_.end() ? json(nullptr)
                                     : json(entry->second->virtual_path);
    }

  private:
    std::filesystem::path root_;
    std::map<data::AssetKey, observer_ptr<const lc::Inspection::AssetEntry>>
      entries_;
    std::map<data::AssetKey, std::shared_ptr<const data::SceneAsset>> scenes_;
  };

  //! A value in the schema's units: an enum is its member's wire value, a
  //! vec3 three numbers, an asset its key.
  auto ToJson(const SchemaValue& value) -> json
  {
    return std::visit(
      []<typename T>(const T& member) -> json {
        if constexpr (std::is_same_v<T, data::AssetKey>) {
          return nostd::to_string(member);
        } else {
          return member;
        }
      },
      value);
  }

  //! A node a script parameter names: a node of the supplying scene, and the
  //! node path below it when that node is an instance root.
  auto ToJson(
    const data::SceneAsset& supplier, const data::ScriptNodeParam& node) -> json
  {
    if (node.node_index == data::pak::scripting::kUnsetNodeParam
      || node.node_index >= supplier.GetNodes().size()) {
      return nullptr;
    }
    auto path = json::array();
    for (const auto& id : supplier.GetNodePath({
           .node_index = node.node_index,
           .node_path = node.node_path,
         })) {
      path.push_back(nostd::to_string(id));
    }
    return {
      {
        "node",
        nostd::to_string(supplier.GetNodes()[node.node_index].node_id),
      },
      { "node_path", std::move(path) },
    };
  }

  auto ToJson(const data::SceneAsset& supplier, const data::ScriptParam& value)
    -> json
  {
    return std::visit(
      [&supplier]<typename T>(const T& member) -> json {
        if constexpr (std::is_same_v<T, Vec2>) {
          return json::array({ member.x, member.y });
        } else if constexpr (std::is_same_v<T, Vec3>) {
          return json::array({ member.x, member.y, member.z });
        } else if constexpr (std::is_same_v<T, Vec4>) {
          return json::array({ member.x, member.y, member.z, member.w });
        } else if constexpr (std::is_same_v<T, data::ScriptAssetParam>) {
          return member.key == data::AssetKey {}
            ? json(nullptr)
            : json(nostd::to_string(member.key));
        } else if constexpr (std::is_same_v<T, data::ScriptNodeParam>) {
          return ToJson(supplier, member);
        } else {
          return member;
        }
      },
      value);
  }

  //! The schema's kind name of the wired `component`.
  auto KindOf(const ComponentId component) -> std::string_view
  {
    for (const auto& property : data::schema::kOverrideProperties) {
      if (property.component == component) {
        return property.component_kind;
      }
    }
    return {};
  }

  //! The changeable properties of the wired `component` that `record` holds;
  //! each is supplied by `defined_by` unless one of `changes` sets it.
  template <typename Record>
  auto Properties(const Record& record, const ComponentId component,
    const data::ChangeSupplier defined_by,
    const std::vector<data::AppliedChange>& changes) -> json
  {
    auto properties = json::array();
    for (const auto& property : data::schema::kOverrideProperties) {
      if (property.component != component || property.asset_sourced) {
        continue;
      }
      const auto value = data::schema::ReadProperty(record, property.id);
      if (!value.has_value()) {
        continue;
      }
      auto supplier = defined_by;
      for (const auto& change : changes) {
        if (change.override_id == property.id) {
          supplier = change.supplier;
        }
      }
      properties.push_back({
        { "key", property.key },
        { "override_id", std::to_underlying(property.id) },
        { "value", ToJson(*value) },
        { "supplier", supplier },
      });
    }
    return properties;
  }

  //! Composes one scene and writes its report row.
  class SceneReport {
  public:
    SceneReport(
      const data::ComposedScene& composed, const RootScenes& root_scenes)
      : composed_(&composed)
      , root_scenes_(&root_scenes)
    {
      for (const auto& [occurrence, entry] :
        std::views::enumerate(composed_->occurrences)) {
        for (const auto index :
          std::views::iota(std::size_t { 0 }, entry.scene->GetNodes().size())) {
          positions_.emplace(
            std::pair { static_cast<std::size_t>(occurrence), index },
            positions_.size());
        }
      }
    }

    auto Write(json& row) const -> void
    {
      auto occurrences = json::array();
      for (const auto& occurrence : composed_->occurrences) {
        occurrences.push_back({
          { "scene", nostd::to_string(occurrence.scene->GetAssetKey()) },
          {
            "virtual_path",
            root_scenes_->VirtualPath(occurrence.scene->GetAssetKey()),
          },
          { "parent", Optional(occurrence.parent) },
          {
            "instance_root",
            occurrence.parent.has_value()
              ? json(Position(*occurrence.parent, occurrence.instance_root))
              : json(nullptr),
          },
        });
      }
      auto nodes = json::array();
      for (const auto& [occurrence, entry] :
        std::views::enumerate(composed_->occurrences)) {
        for (const auto& node : entry.nodes) {
          nodes.push_back(Node(static_cast<std::size_t>(occurrence), node));
        }
      }
      row.at("occurrences") = std::move(occurrences);
      row.at("nodes") = std::move(nodes);
      row.at("unresolved") = Unresolved();
      row.at("issues") = Issues();
    }

  private:
    template <typename T>
    static auto Optional(const std::optional<T>& value) -> json
    {
      return value.has_value() ? json(*value) : json(nullptr);
    }

    [[nodiscard]] auto Position(const std::size_t occurrence,
      const world::SceneNodeIndexT index) const -> std::size_t
    {
      return positions_.at({ occurrence, index });
    }

    [[nodiscard]] auto Node(const std::size_t occurrence,
      const data::ComposedNode& node) const -> json
    {
      const auto& entry = composed_->occurrences.at(occurrence);
      const auto& scene = *entry.scene;
      const auto& outer = *composed_->occurrences.front().scene;
      auto path = json::array();
      for (const auto& id : node.path) {
        path.push_back(nostd::to_string(id));
      }
      auto parent = json(nullptr);
      if (node.node.parent_index != node.index) {
        parent = Position(occurrence, node.node.parent_index);
      } else if (entry.parent.has_value()) {
        parent = Position(*entry.parent, entry.instance_root);
      }
      auto row = json {
        { "occurrence", occurrence },
        { "index", node.index },
        { "node_id", nostd::to_string(node.node.node_id) },
        {
          "root",
          nostd::to_string(
            outer.GetNodes()[entry.level == 0U ? node.index : entry.outer_root]
              .node_id),
        },
        { "node_path", std::move(path) },
        { "name", scene.GetNodeName(node.node) },
        { "parent", std::move(parent) },
        { "defining_scene", nostd::to_string(scene.GetAssetKey()) },
        { "components", Components(occurrence, node) },
        { "scripts", Scripts(entry, node.index) },
      };
      for (const auto& instance :
        scene.GetComponents<world::SceneInstanceRecord>()) {
        if (instance.node_index == node.index) {
          row["instance"]
            = { { "source", nostd::to_string(instance.scene_key) } };
        }
      }
      if (node.renderable.has_value()) {
        row["geometry"] = Geometry(*node.renderable, node.overrides);
      }
      return row;
    }

    [[nodiscard]] static auto Components(
      const std::size_t occurrence, const data::ComposedNode& node) -> json
    {
      auto components = json::array();
      const auto add
        = [&](const auto& record, const ComponentId component) -> void {
        components.push_back({
          { "kind", KindOf(component) },
          {
            "properties",
            Properties(record, component, occurrence, node.changes),
          },
        });
      };
      add(node.node, ComponentId::kTransform);
      add(node.node, ComponentId::kNode);
      if (node.perspective_camera.has_value()) {
        add(*node.perspective_camera, ComponentId::kPerspectiveCamera);
      }
      if (node.orthographic_camera.has_value()) {
        add(*node.orthographic_camera, ComponentId::kOrthographicCamera);
      }
      if (node.directional_light.has_value()) {
        add(*node.directional_light, ComponentId::kDirectionalLight);
      }
      if (node.point_light.has_value()) {
        add(*node.point_light, ComponentId::kPointLight);
      }
      if (node.spot_light.has_value()) {
        add(*node.spot_light, ComponentId::kSpotLight);
      }
      if (node.local_fog_volume.has_value()) {
        add(*node.local_fog_volume, ComponentId::kLocalFogVolume);
      }
      return components;
    }

    //! The geometry and the values of it the node replaces; an override's
    //! value is held by its supplier's scene.
    [[nodiscard]] auto Geometry(const world::RenderableRecord& renderable,
      const std::vector<data::ComposedOverride>& overrides) const -> json
    {
      auto values = json::array();
      for (const auto& entry : overrides) {
        const auto* property
          = data::schema::FindOverrideProperty(entry.record.override_id);
        if (property == nullptr) {
          continue;
        }
        const auto& supplier = *composed_->occurrences.at(entry.supplier).scene;
        values.push_back({
          { "key", property->key },
          { "override_id", std::to_underlying(property->id) },
          {
            "part_id",
            property->part_kind == data::schema::PartKind::kNone
              ? json(nullptr)
              : json(nostd::to_string(entry.record.part_id)),
          },
          { "value", OverrideValue(*property, entry.record, supplier) },
          { "supplier", entry.supplier },
        });
      }
      return {
        { "asset_key", nostd::to_string(renderable.geometry_key) },
        { "overrides", std::move(values) },
      };
    }

    [[nodiscard]] static auto OverrideValue(const OverrideProperty& property,
      const world::OverrideRecord& record, const data::SceneAsset& supplier)
      -> json
    {
      using data::schema::ValueType;
      switch (property.type) {
      case ValueType::kBool:
        return record.value.as_bool != 0U;
      case ValueType::kAsset:
        return nostd::to_string(record.value.as_asset);
      case ValueType::kLodPolicy: {
        const auto policies
          = supplier.GetComponents<data::pak::geometry::LodPolicyRecord>();
        const auto index = record.value.as_record_index.get();
        if (index >= policies.size()) {
          throw std::runtime_error("LOD policy override names no record.");
        }
        return import::internal::WriteLodPolicy(policies[index]);
      }
      case ValueType::kInt:
      case ValueType::kFloat:
      case ValueType::kVec3:
      case ValueType::kEnum:
      case ValueType::kString:
        break;
      }
      throw std::runtime_error("Unsupported asset-sourced override type.");
    }

    [[nodiscard]] auto Scripts(const data::ComposedOccurrence& occurrence,
      const world::SceneNodeIndexT index) const -> json
    {
      auto scripts = json::array();
      for (const auto& slot : occurrence.slots) {
        if (slot.node_index != index) {
          continue;
        }
        auto parameters = json::array();
        for (const auto& parameter : slot.parameters) {
          const auto& supplier
            = *composed_->occurrences.at(parameter.supplier).scene;
          parameters.push_back({
            { "key", parameter.key },
            { "value", ToJson(supplier, parameter.value) },
            { "supplier", parameter.supplier },
          });
        }
        scripts.push_back({
          { "slot_id", nostd::to_string(slot.slot_id) },
          { "script", nostd::to_string(slot.script) },
          { "execution_order", slot.execution_order },
          { "parameters", std::move(parameters) },
        });
      }
      return scripts;
    }

    [[nodiscard]] auto Unresolved() const -> json
    {
      auto unresolved = json::array();
      for (const auto& entry : composed_->unresolved) {
        auto path = json::array();
        for (const auto& id : entry.change.path) {
          path.push_back(nostd::to_string(id));
        }
        auto row = json {
          { "supplier", entry.supplier },
          { "instance_root", Position(entry.supplier, entry.instance_root) },
          { "node_path", std::move(path) },
        };
        std::visit(
          [&row]<typename T>(const T& change) -> void {
            if constexpr (std::is_same_v<T, data::ParameterChange>) {
              row["parameter"] = {
                { "slot_id", nostd::to_string(change.slot_id) },
                { "key", change.key },
              };
            } else {
              const auto* property
                = data::schema::FindOverrideProperty(change.override_id);
              row["property"] = {
                { "override_id", std::to_underlying(change.override_id) },
                {
                  "key",
                  property == nullptr ? json(nullptr) : json(property->key),
                },
                {
                  "part_id",
                  property == nullptr
                      || property->part_kind == data::schema::PartKind::kNone
                    ? json(nullptr)
                    : json(nostd::to_string(change.part_id)),
                },
              };
            }
          },
          entry.change.change);
        unresolved.push_back(std::move(row));
      }
      return unresolved;
    }

    [[nodiscard]] auto Issues() const -> json
    {
      using data::CompositionIssueCode;
      auto issues = json::array();
      for (const auto& issue : composed_->issues) {
        const auto names_scene = issue.code == CompositionIssueCode::kCycle
          || issue.code == CompositionIssueCode::kSceneMissing;
        issues.push_back({
          { "code", data::to_string(issue.code) },
          { "occurrence", issue.occurrence },
          {
            "node",
            issue.node_index.has_value()
              ? json(Position(issue.occurrence, *issue.node_index))
              : json(nullptr),
          },
          {
            "scene",
            names_scene ? json(nostd::to_string(issue.scene)) : json(nullptr),
          },
        });
      }
      return issues;
    }

    observer_ptr<const data::ComposedScene> composed_;
    observer_ptr<const RootScenes> root_scenes_;
    //! A node's position in the report, by occurrence and index.
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> positions_;
  };

} // namespace

auto BuildSceneMetadataCommand(SceneMetadataOptions& options)
  -> std::shared_ptr<clap::Command>
{
  auto root = clap::Option::Positional("cooked_root")
                .About("Loose cooked root directory")
                .Required()
                .WithValue<std::string>()
                .StoreTo(&options.cooked_root)
                .Build();
  auto output = clap::Option::WithKey("output")
                  .Long("output")
                  .About("Destination JSON scene metadata report")
                  .Required()
                  .WithValue<std::string>()
                  .StoreTo(&options.output)
                  .Build();
  auto virtual_path = clap::Option::WithKey("virtual-path")
                        .Long("virtual-path")
                        .About("Compose only this scene virtual path")
                        .WithValue<std::string>()
                        .StoreTo(&options.virtual_path)
                        .Build();
  return clap::CommandBuilder("scenes")
    .About("Compose native scenes: every node, owned and instanced, with its "
           "values and the scene that supplies each.")
    .WithPositionalArguments(root)
    .WithOption(std::move(output))
    .WithOption(std::move(virtual_path));
}

auto RunSceneMetadataReport(const SceneMetadataOptions& options)
  -> toolkit::ToolResult
{
  const auto root
    = std::filesystem::absolute(options.cooked_root).lexically_normal();
  const auto output_path
    = std::filesystem::absolute(options.output).lexically_normal();
  if (output_path == root / "container.index.bin") {
    throw std::runtime_error(
      "Scene metadata output must not replace its source index.");
  }
  lc::Inspection inspection;
  inspection.LoadFromRoot(root);
  auto root_scenes = RootScenes(root, inspection);
  auto scenes = json::array();
  // Each scene that could not be read, as `scene '<path>' (<key>): <why>`.
  auto failures = std::vector<std::string> {};
  for (const auto& entry : inspection.Assets()) {
    const auto relative
      = std::filesystem::path(entry.descriptor_relpath).lexically_normal();
    if (output_path == root / relative) {
      throw std::runtime_error(
        "Scene metadata output must not replace a source descriptor.");
    }
    if (entry.asset_type != static_cast<uint8_t>(data::AssetType::kScene)
      || (!options.virtual_path.empty()
        && entry.virtual_path != options.virtual_path)) {
      continue;
    }
    auto row = json {
      { "asset_key", nostd::to_string(entry.key) },
      { "virtual_path", entry.virtual_path },
      { "descriptor_version", nullptr },
      { "occurrences", json::array() },
      { "nodes", json::array() },
      { "unresolved", json::array() },
      { "issues", json::array() },
      { "complete", false },
    };
    try {
      auto scene = root_scenes.Load(entry.key);
      if (!scene) {
        throw std::runtime_error("The scene is not in its root.");
      }
      row.at("descriptor_version") = scene->GetHeader().version;
      const auto composed
        = data::ComposeScene(std::move(scene), root_scenes.Resolver());
      SceneReport(composed, root_scenes).Write(row);
      row.at("complete") = true;
    } catch (const std::exception& error) {
      row.emplace("diagnostic", error.what());
      failures.push_back(fmt::format("scene '{}' ({}): {}", entry.virtual_path,
        nostd::to_string(entry.key), error.what()));
    }
    scenes.push_back(std::move(row));
  }
  const auto report = json {
    { "schema", "oxygen.cooked-scenes.v2" },
    { "source_key", nostd::to_string(inspection.Guid()) },
    { "complete", failures.empty() },
    { "scenes", std::move(scenes) },
  };
  // An output stream truncates the file it opens.
  std::ofstream output(output_path, std::ios::binary);
  output << report.dump(2) << '\n';
  output.close();
  if (!output) {
    throw std::runtime_error("Could not write scene metadata report.");
  }
  if (!failures.empty()) {
    auto message = std::string {};
    for (const auto& failure : failures) {
      message += message.empty() ? failure : "; " + failure;
    }
    return std::unexpected(toolkit::ToolError::Failed(std::move(message)));
  }
  return {};
}

} // namespace oxygen::content::inspection
