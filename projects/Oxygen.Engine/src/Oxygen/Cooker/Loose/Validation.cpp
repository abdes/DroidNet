//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstring>
#include <exception>
#include <filesystem>
#include <ios>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetValidation.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Loose/Types.h>
#include <Oxygen/Cooker/Loose/Validation.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormatVersions.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::lc {

auto ValidateRoot(
  const std::filesystem::path& cooked_root, const IntegrityCheck check) -> void
{
  using oxygen::data::AssetType;
  using oxygen::data::pak::core::AssetHeader;
  using oxygen::data::pak::geometry::GeometryAssetDesc;
  using oxygen::data::pak::input::InputMappingContextAssetDesc;
  using oxygen::data::pak::render::MaterialAssetDesc;
  using oxygen::data::pak::scripting::ScriptAssetDesc;
  using oxygen::data::pak::world::SceneAssetDesc;
  using oxygen::serio::FileStream;
  using oxygen::serio::Reader;

  const auto index = LooseCookedIndex::LoadFromRoot(cooked_root);
  index.ValidateContent(cooked_root, check);
  oxygen::content::lc::Inspection inspection;
  inspection.LoadFromRoot(cooked_root);
  for (const auto& file : inspection.Files()) {
    if (file.kind == FileKind::kAuxiliary
      && std::filesystem::path(file.relpath).extension() == ".otex") {
      static_cast<void>(inspection.ReadTextureDescriptor(file.relpath));
    }
  }
  std::unordered_map<data::AssetKey, std::unique_ptr<data::GeometryAsset>>
    geometries;
  std::vector<std::unique_ptr<data::SceneAsset>> scenes;

  const auto entries
    = [&index](const FileKind kind, const size_t entry_size) -> uint64_t {
    const auto bytes = index.FindFileSize(kind);
    if (!bytes) {
      return 0U;
    }
    if (*bytes % entry_size != 0U) {
      throw std::runtime_error("Resource table contains a partial record");
    }
    return *bytes / entry_size;
  };
  const data::ResourceTableCounts table_counts {
    .buffers = entries(
      FileKind::kBuffersTable, sizeof(data::pak::core::BufferResourceDesc)),
    .textures = entries(
      FileKind::kTexturesTable, sizeof(data::pak::core::TextureResourceDesc)),
    .scripts = entries(FileKind::kScriptsTable,
      sizeof(data::pak::scripting::ScriptResourceDesc)),
    .physics = entries(
      FileKind::kPhysicsTable, sizeof(data::pak::physics::PhysicsResourceDesc)),
  };

  for (const auto& asset : inspection.Assets()) {
    if (asset.descriptor_relpath.empty()) {
      throw std::runtime_error("asset descriptor path is missing");
    }

    const auto descriptor_path = cooked_root / asset.descriptor_relpath;
    if (!std::filesystem::exists(base::ToNativePath(descriptor_path))) {
      throw std::runtime_error(
        "descriptor file does not exist: " + descriptor_path.generic_string());
    }

    FileStream<> stream(descriptor_path, std::ios::in);
    const auto descriptor_size_result = stream.Size();
    if (!descriptor_size_result) {
      throw std::runtime_error("failed to query descriptor file size");
    }
    const auto descriptor_size = descriptor_size_result.value();
    if (descriptor_size != asset.descriptor_size) {
      throw std::runtime_error(
        "descriptor size mismatch for " + descriptor_path.generic_string());
    }

    if (descriptor_size < sizeof(AssetHeader)) {
      throw std::runtime_error("descriptor is smaller than AssetHeader: "
        + descriptor_path.generic_string());
    }

    Reader<FileStream<>> reader(stream);
    auto pack = reader.ScopedAlignment(1);
    auto blob = reader.ReadBlob(sizeof(AssetHeader));
    if (!blob) {
      throw std::runtime_error("failed to read descriptor header");
    }

    AssetHeader header {};
    std::memcpy(&header, blob->data(), sizeof(header));

    if (header.asset_type != asset.asset_type) {
      throw std::runtime_error("descriptor header asset_type mismatch for "
        + descriptor_path.generic_string());
    }

    const auto asset_type = static_cast<AssetType>(asset.asset_type);
    const auto current_version = data::pak::CurrentAssetVersion(asset_type);
    if (!current_version || header.version != *current_version) {
      throw std::runtime_error("Unsupported descriptor version in "
        + descriptor_path.generic_string()
        + "; re-cook with the current tools");
    }
    size_t min_size = sizeof(AssetHeader);
    switch (asset_type) {
    case AssetType::kMaterial:
      min_size = sizeof(MaterialAssetDesc);
      break;
    case AssetType::kGeometry:
      min_size = sizeof(GeometryAssetDesc);
      break;
    case AssetType::kScene:
      min_size = sizeof(SceneAssetDesc);
      break;
    case AssetType::kScript:
      min_size = sizeof(ScriptAssetDesc);
      break;
    case AssetType::kInputAction:
      min_size = sizeof(oxygen::data::pak::input::InputActionAssetDesc);
      break;
    case AssetType::kInputMappingContext:
      min_size = sizeof(InputMappingContextAssetDesc);
      break;
    case AssetType::kPhysicsMaterial:
      min_size = sizeof(oxygen::data::pak::physics::PhysicsMaterialAssetDesc);
      break;
    case AssetType::kCollisionShape:
      min_size = sizeof(oxygen::data::pak::physics::CollisionShapeAssetDesc);
      break;
    case AssetType::kPhysicsScene:
      min_size = sizeof(oxygen::data::pak::physics::PhysicsSceneAssetDesc);
      break;
    default:
      break;
    }

    if (descriptor_size < min_size) {
      throw std::runtime_error("descriptor smaller than minimum expected size: "
        + descriptor_path.generic_string());
    }

    const auto references = index.FindAssetReferences(asset.key);
    if (!references.has_value()) {
      throw std::runtime_error("Asset reference inventory is missing");
    }
    if (const auto valid = references->ValidateResourceBounds(table_counts);
      !valid) {
      throw std::runtime_error(
        descriptor_path.generic_string() + ": " + valid.error());
    }
    if (asset_type == AssetType::kScene || asset_type == AssetType::kMaterial
      || asset_type == AssetType::kGeometry) {
      if (!reader.Seek(0)) {
        throw std::runtime_error(
          "failed to rewind descriptor: " + descriptor_path.generic_string());
      }
      try {
        // Runtime parse-only loaders own descriptor versions and value checks.
        // Unmounted external dependencies do not invalidate descriptor data.
        const LoaderContext context {
          .current_asset_key = asset.key,
          .desc_reader = &reader,
          .asset_references = observer_ptr(&*references),
          .work_offline = true,
          .parse_only = true,
        };
        if (asset_type == AssetType::kGeometry) {
          geometries.emplace(asset.key, loaders::LoadGeometryAsset(context));
        } else if (asset_type == AssetType::kScene) {
          scenes.push_back(loaders::LoadSceneAsset(context));
        } else {
          static_cast<void>(loaders::LoadMaterialAsset(context));
        }
        const auto consumed = reader.Position();
        if (!consumed || *consumed != descriptor_size) {
          throw std::runtime_error(
            "Asset descriptor contains unexpected trailing bytes");
        }
      } catch (const std::exception& error) {
        throw std::runtime_error("invalid asset descriptor '"
          + descriptor_path.generic_string() + "': " + error.what());
      }
    } else {
      if (!reader.Seek(0)) {
        throw std::runtime_error("Could not rewind asset descriptor");
      }
      const auto bytes = reader.ReadBlob(descriptor_size);
      if (!bytes) {
        throw std::runtime_error("Could not read complete asset descriptor");
      }
      ValidateAssetDescriptor(asset_type, asset.key, *bytes, *references);
    }
  }

  for (const auto& scene : scenes) {
    std::unordered_map<data::pak::world::SceneNodeIndexT, data::AssetKey>
      geometry_by_node;
    for (const auto& renderable :
      scene->GetComponents<data::pak::world::RenderableRecord>()) {
      geometry_by_node.emplace(renderable.node_index, renderable.geometry_key);
    }
    for (const auto& assignment :
      scene->GetComponents<data::pak::world::MaterialOverrideRecord>()) {
      const auto node = geometry_by_node.find(assignment.node_index);
      if (node == geometry_by_node.end()) {
        throw std::runtime_error("material override has no renderable in scene "
          + nostd::to_string(scene->GetAssetKey()));
      }
      const auto found = geometries.find(node->second);
      if (found == geometries.end()) {
        continue; // Other mounted roots are outside this root's inventory.
      }
      const auto& geometry = *found->second;
      if (geometry.FindMaterialSlot(assignment.slot_id) == nullptr) {
        throw std::runtime_error("unknown material slot "
          + data::to_string(assignment.slot_id) + " in scene "
          + nostd::to_string(scene->GetAssetKey()));
      }
      if (geometry.MaterialSlots().layout_revision
        != assignment.layout_revision) {
        throw std::runtime_error(
          "material slot layout requires repair in scene "
          + nostd::to_string(scene->GetAssetKey()));
      }
    }
  }
}

} // namespace oxygen::content::lc
