//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/Internal/ContentSourceRegistry.h>
#include <Oxygen/Content/Internal/ContentSourceView.h>
#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Content/Internal/LooseCookedSource.h>
#include <Oxygen/Content/Internal/SceneCatalogQueryService.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneChangeValues.h>
#include <Oxygen/Data/SceneComposition.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneChanges.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>

namespace oxygen::content::internal {

namespace {

  //! The scene's name.
  /*!
   A scene whose descriptor cannot be read is still listed, with an empty name
   and a warning, so loading it reports why instead of the scene vanishing.
  */
  auto ReadSceneName(const IContentSource& source, const data::AssetKey& key)
    -> std::string
  {
    auto desc_reader = source.CreateAssetDescriptorReader(key);
    if (!desc_reader) {
      LOG_F(
        WARNING, "scene {} has no readable descriptor", data::to_string(key));
      return {};
    }
    try {
      const auto base_pos = desc_reader->Position();
      loaders::CheckLoaderResult(base_pos, "scene", "position");
      auto header = data::pak::core::AssetHeader {};
      loaders::LoadAssetHeader(*desc_reader, header);
      return loaders::ResolveDescriptorString(
        loaders::ReadDescriptorStrings(*desc_reader, *base_pos, header),
        header.name_offset, "scene name");
    } catch (const std::runtime_error& error) {
      LOG_F(WARNING, "scene {} name is unreadable: {}", data::to_string(key),
        error.what());
      return {};
    }
  }

  //! The mounted scene descriptors one query reads, each parsed once without
  //! its dependencies, and the virtual paths of the assets they name.
  class MountedScenes final {
  public:
    MountedScenes(observer_ptr<const ContentSourceRegistry> source_registry,
      std::shared_ptr<const ContentSourceView> view)
      : source_registry_(source_registry)
      , view_(std::move(view))
    {
    }

    //! The scene's descriptor parsed alone, or null when no mounted source
    //! provides a readable scene for `key`.
    auto Load(const data::AssetKey& key)
      -> std::shared_ptr<const data::SceneAsset>
    {
      if (const auto found = loaded_.find(key); found != loaded_.end()) {
        return found->second;
      }
      auto scene = this->Parse(key);
      loaded_.emplace(key, scene);
      return scene;
    }

    //! The asset with its virtual path in the mounted source that provides
    //! it; an empty path when none does.
    [[nodiscard]] auto AssetOf(const data::AssetKey& key) const
      -> IAssetLoader::SceneNodeAsset
    {
      auto asset
        = IAssetLoader::SceneNodeAsset { .key = key, .virtual_path = {} };
      const auto source_id = view_->ResolveAsset(key);
      if (!source_id.has_value()) {
        return asset;
      }
      if (const auto source = source_registry_->AcquireSource(*source_id)) {
        asset.virtual_path
          = source->ResolveVirtualPath(key).value_or(std::string {});
      }
      return asset;
    }

  private:
    [[nodiscard]] auto Parse(const data::AssetKey& key) const
      -> std::shared_ptr<const data::SceneAsset>
    {
      const auto source_id = view_->ResolveAsset(key);
      if (!source_id.has_value()) {
        return nullptr;
      }
      const auto source = source_registry_->AcquireSource(*source_id);
      if (!source || source->GetAssetType(key) != data::AssetType::kScene) {
        return nullptr;
      }
      const auto desc_reader = source->CreateAssetDescriptorReader(key);
      if (!desc_reader) {
        LOG_F(
          WARNING, "scene {} has no readable descriptor", data::to_string(key));
        return nullptr;
      }
      try {
        const auto references = source->ReadAssetReferences(key);
        const LoaderContext context {
          .current_asset_key = key,
          .source_instance = *source_id,
          .desc_reader = desc_reader.get(),
          .asset_references = observer_ptr(&references),
          .work_offline = true,
          .parse_only = true,
        };
        return loaders::LoadSceneAsset(context);
      } catch (const std::runtime_error& error) {
        LOG_F(WARNING, "scene {} descriptor is unreadable: {}",
          data::to_string(key), error.what());
        return nullptr;
      }
    }

    observer_ptr<const ContentSourceRegistry> source_registry_;
    std::shared_ptr<const ContentSourceView> view_;
    std::unordered_map<data::AssetKey, std::shared_ptr<const data::SceneAsset>>
      loaded_;
  };

  //! Reads a mounted scene's hierarchy from its composition: each occurrence's
  //! nodes in turn, with the geometry the composition gives them.
  class SceneHierarchyReader final {
  public:
    explicit SceneHierarchyReader(MountedScenes& scenes)
      : scenes_(&scenes)
    {
    }

    [[nodiscard]] auto Read(const data::AssetKey& key) const
      -> std::optional<IAssetLoader::SceneHierarchy>
    {
      if (!scenes_->Load(key)) {
        return std::nullopt;
      }
      const auto composed = data::ComposeInstance(
        data::InstanceComposition {
          .source = key,
          .instance_root = 0U,
          .changes = {},
          .ancestors = {},
        },
        [scenes = scenes_](const data::AssetKey& scene)
          -> std::shared_ptr<const data::SceneAsset> {
          return scenes->Load(scene);
        });

      auto hierarchy = IAssetLoader::SceneHierarchy { .scene_key = key };
      // The index of each occurrence's first node in the hierarchy.
      auto bases = std::vector<uint32_t> {};
      bases.reserve(composed.occurrences.size());
      for (const auto& occurrence : composed.occurrences) {
        const auto base = static_cast<uint32_t>(hierarchy.nodes.size());
        bases.push_back(base);
        // An occurrence's roots are children of its instance root.
        const auto instance_root = occurrence.parent.has_value()
          ? std::optional(
              bases.at(*occurrence.parent) + occurrence.instance_root)
          : std::nullopt;
        if (instance_root.has_value()) {
          hierarchy.nodes.at(*instance_root).instance
            = occurrence.scene->GetAssetKey();
        }
        for (const auto& [index, node] :
          std::views::enumerate(occurrence.nodes)) {
          const auto own = static_cast<uint32_t>(index);
          hierarchy.nodes.push_back(IAssetLoader::SceneHierarchyNode {
            .path = node.path,
            .name = std::string(occurrence.scene->GetNodeName(node.node)),
            .parent = node.node.parent_index == own
              ? instance_root
              : std::optional(base + node.node.parent_index),
            .scene_key = occurrence.scene->GetAssetKey(),
            .instance = std::nullopt,
            .geometry = node.renderable.has_value()
              ? std::optional(scenes_->AssetOf(node.renderable->geometry_key))
              : std::nullopt,
          });
        }
      }

      // Issues on values are the value reader's; an instance the composition
      // could not follow is one of the hierarchy's.
      for (const auto& issue : composed.issues) {
        const auto code = HierarchyIssueOf(issue.code);
        if (!code.has_value() || !issue.node_index.has_value()
          || issue.occurrence >= bases.size()) {
          continue;
        }
        const auto node = bases.at(issue.occurrence) + *issue.node_index;
        hierarchy.nodes.at(node).instance = issue.scene;
        hierarchy.issues.push_back(IAssetLoader::SceneHierarchyIssue {
          .code = *code,
          .node = node,
          .scene_key = issue.scene,
        });
      }
      return hierarchy;
    }

  private:
    [[nodiscard]] static auto HierarchyIssueOf(
      const data::CompositionIssueCode code)
      -> std::optional<IAssetLoader::SceneHierarchyIssueCode>
    {
      switch (code) {
      case data::CompositionIssueCode::kSceneMissing:
        return IAssetLoader::SceneHierarchyIssueCode::kSceneUnavailable;
      case data::CompositionIssueCode::kCycle:
        return IAssetLoader::SceneHierarchyIssueCode::kCycle;
      case data::CompositionIssueCode::kChangeInvalid:
      case data::CompositionIssueCode::kRecordInvalid:
        return std::nullopt;
      }
      return std::nullopt;
    }

    observer_ptr<MountedScenes> scenes_;
  };

  //! The value of the property `id` in whichever of the node's records holds
  //! it.
  auto ReadComposedProperty(
    const data::ComposedNode& node, const data::schema::OverrideId id)
    -> std::optional<data::schema::SchemaValue>
  {
    using data::schema::ReadProperty;
    const auto read
      = [id](const auto& record) -> std::optional<data::schema::SchemaValue> {
      return record.has_value() ? ReadProperty(*record, id) : std::nullopt;
    };
    if (auto value = ReadProperty(node.node, id)) {
      return value;
    }
    const auto candidates = std::array {
      read(node.perspective_camera),
      read(node.orthographic_camera),
      read(node.directional_light),
      read(node.point_light),
      read(node.spot_light),
      read(node.local_fog_volume),
    };
    const auto found = std::ranges::find_if(candidates,
      [](const auto& candidate) -> bool { return candidate.has_value(); });
    return found != candidates.end() ? *found : std::nullopt;
  }

  //! Reads the effective values of one node of a mounted scene's
  //! composition.
  class SceneNodeReader final {
  public:
    explicit SceneNodeReader(MountedScenes& scenes)
      : scenes_(&scenes)
    {
    }

    [[nodiscard]] auto Read(
      const data::AssetKey& key, const std::span<const data::NodeId> path) const
      -> std::optional<IAssetLoader::SceneNodeValues>
    {
      if (path.empty()) {
        return std::nullopt;
      }
      const auto composed = data::ComposeInstance(
        data::InstanceComposition {
          .source = key,
          .instance_root = 0U,
          .changes = {},
          .ancestors = {},
        },
        [scenes = scenes_](const data::AssetKey& scene)
          -> std::shared_ptr<const data::SceneAsset> {
          return scenes->Load(scene);
        });
      for (const auto& occurrence : composed.occurrences) {
        const auto node = std::ranges::find_if(occurrence.nodes,
          [path](const data::ComposedNode& candidate) -> bool {
            return std::ranges::equal(candidate.path, path);
          });
        if (node != occurrence.nodes.end()) {
          return this->ValuesOf(composed, occurrence, *node);
        }
      }
      return std::nullopt;
    }

  private:
    [[nodiscard]] auto ValuesOf(const data::ComposedScene& composed,
      const data::ComposedOccurrence& occurrence,
      const data::ComposedNode& node) const -> IAssetLoader::SceneNodeValues
    {
      auto values = IAssetLoader::SceneNodeValues {
        .name = std::string(occurrence.scene->GetNodeName(node.node)),
        .geometry = std::nullopt,
        .properties = {},
      };
      if (node.renderable.has_value()) {
        values.geometry = scenes_->AssetOf(node.renderable->geometry_key);
      }
      for (const auto& property : data::schema::kOverrideProperties) {
        if (const auto value = ReadComposedProperty(node, property.id)) {
          values.properties.push_back(IAssetLoader::SceneNodeProperty {
            .override_id = property.id,
            .part_id = {},
            .layout_revision = {},
            .value = this->ToNodeValue(*value),
          });
        }
      }
      for (const auto& entry : node.overrides) {
        if (auto value = this->OverrideValueOf(composed, entry)) {
          values.properties.push_back(IAssetLoader::SceneNodeProperty {
            .override_id = entry.record.override_id,
            .part_id = entry.record.part_id,
            .layout_revision = entry.record.layout_revision,
            .value = std::move(*value),
          });
        }
      }
      return values;
    }

    [[nodiscard]] auto ToNodeValue(const data::schema::SchemaValue& value) const
      -> IAssetLoader::SceneNodeValue
    {
      return std::visit(
        [this](const auto& held) -> IAssetLoader::SceneNodeValue {
          if constexpr (std::is_same_v<std::decay_t<decltype(held)>,
                          data::AssetKey>) {
            return scenes_->AssetOf(held);
          } else {
            return held;
          }
        },
        value);
    }

    //! The value of a geometry value the node replaces, or none when it names
    //! a LOD policy its scene does not hold.
    [[nodiscard]] auto OverrideValueOf(const data::ComposedScene& composed,
      const data::ComposedOverride& entry) const
      -> std::optional<IAssetLoader::SceneNodeValue>
    {
      using data::schema::ValueMember;
      const auto* property
        = data::schema::FindOverrideProperty(entry.record.override_id);
      if (property == nullptr) {
        return std::nullopt;
      }
      const auto& value = entry.record.value;
      switch (property->member) {
      case ValueMember::kBool:
        return value.as_bool != 0U;
      case ValueMember::kInt32:
        return value.as_int32;
      case ValueMember::kFloat:
        return value.as_float;
      case ValueMember::kVec3:
        return std::array {
          value.as_vec3[0],
          value.as_vec3[1],
          value.as_vec3[2],
        };
      case ValueMember::kAsset:
        return scenes_->AssetOf(value.as_asset);
      case ValueMember::kRecordIndex: {
        if (entry.supplier >= composed.occurrences.size()) {
          return std::nullopt;
        }
        // The supplier's scene holds the policy table the index names.
        const auto policies
          = composed.occurrences.at(entry.supplier)
              .scene->GetComponents<data::pak::geometry::LodPolicyRecord>();
        const auto index = value.as_record_index.get();
        if (index >= policies.size()) {
          return std::nullopt;
        }
        return policies.subspan(index, 1).front();
      }
      case ValueMember::kQuat:
      case ValueMember::kStringOffset:
        // No geometry value is a rotation or a string.
        return std::nullopt;
      }
      return std::nullopt;
    }

    observer_ptr<MountedScenes> scenes_;
  };

} // namespace

auto SceneCatalogQueryService::EnumerateMountedScenes(
  const ContentSourceRegistry& source_registry) const
  -> std::vector<IAssetLoader::MountedSceneEntry>
{
  std::vector<IAssetLoader::MountedSceneEntry> scenes;
  const auto view = source_registry.CaptureView();
  if (!view) {
    return scenes;
  }
  scenes.reserve(view->Layers().size());
  for (const auto& layer : view->Layers()) {
    const auto source = source_registry.AcquireSource(layer.id);
    if (!source) {
      continue;
    }
    const auto source_id = layer.id;
    const auto source_key = source->GetSourceKey();
    const auto source_path = source->SourcePath();
    const auto source_kind
      = source->GetTypeId() == LooseCookedSource::ClassTypeId()
      ? IAssetLoader::ContentSourceKind::kLooseCooked
      : IAssetLoader::ContentSourceKind::kPak;

    const auto asset_count = source->GetAssetCount();
    for (size_t i = 0; i < asset_count; ++i) {
      const auto scene_key_opt
        = source->GetAssetKeyByIndex(static_cast<uint32_t>(i));
      if (!scene_key_opt.has_value()) {
        continue;
      }

      if (source->GetAssetType(*scene_key_opt) != data::AssetType::kScene
        || view->ResolveAsset(*scene_key_opt) != source_id) {
        continue;
      }

      IAssetLoader::MountedSceneEntry scene_entry {};
      scene_entry.scene_key = *scene_key_opt;
      scene_entry.source_key = source_key;
      scene_entry.source_id = source_id;
      scene_entry.source_kind = source_kind;
      scene_entry.source_path = source_path;
      scene_entry.display_name = ReadSceneName(*source, *scene_key_opt);

      if (const auto vpath = source->ResolveVirtualPath(*scene_key_opt);
        vpath.has_value()) {
        scene_entry.virtual_path = *vpath;
      }

      scenes.push_back(std::move(scene_entry));
    }
  }

  return scenes;
}

auto SceneCatalogQueryService::ReadSceneHierarchy(
  const ContentSourceRegistry& source_registry, const data::AssetKey& key) const
  -> std::optional<IAssetLoader::SceneHierarchy>
{
  auto view = source_registry.CaptureView();
  if (!view) {
    return std::nullopt;
  }
  auto scenes = MountedScenes(observer_ptr(&source_registry), std::move(view));
  return SceneHierarchyReader(scenes).Read(key);
}

auto SceneCatalogQueryService::ReadSceneNode(
  const ContentSourceRegistry& source_registry, const data::AssetKey& key,
  const std::span<const data::NodeId> path) const
  -> std::optional<IAssetLoader::SceneNodeValues>
{
  auto view = source_registry.CaptureView();
  if (!view) {
    return std::nullopt;
  }
  auto scenes = MountedScenes(observer_ptr(&source_registry), std::move(view));
  return SceneNodeReader(scenes).Read(key, path);
}

} // namespace oxygen::content::internal
