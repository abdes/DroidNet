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
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Loose/Validation.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::lc {

auto ValidateRoot(const std::filesystem::path& cooked_root) -> void
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

  oxygen::content::lc::Inspection inspection;
  inspection.LoadFromRoot(cooked_root);
  std::unordered_map<data::AssetKey, std::unique_ptr<data::GeometryAsset>>
    geometries;
  std::vector<std::unique_ptr<data::SceneAsset>> scenes;

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
      } catch (const std::exception& error) {
        throw std::runtime_error("invalid asset descriptor '"
          + descriptor_path.generic_string() + "': " + error.what());
      }
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
