//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetValidation.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/Validate.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormatVersions.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/StableId.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/RegionStream.h>

namespace oxygen::content::cooked {

namespace {
  //! The refusal of the descriptor `name` for `defect`.
  auto Refusal(const data::CookedDefect defect, const std::string& name,
    const std::string_view problem) -> data::CookedFormatError
  {
    return { defect, "descriptor '" + name + "' " + std::string(problem) };
  }
} // namespace

auto Validate(const CookedContent& content, const lc::IntegrityCheck check)
  -> void
{
  using oxygen::data::AssetType;
  using oxygen::data::pak::core::AssetHeader;
  using oxygen::data::pak::geometry::GeometryAssetDesc;
  using oxygen::data::pak::input::InputMappingContextAssetDesc;
  using oxygen::data::pak::render::MaterialAssetDesc;
  using oxygen::data::pak::scripting::ScriptAssetDesc;
  using oxygen::data::pak::world::SceneAssetDesc;
  using oxygen::serio::MemoryFile;
  using oxygen::serio::Reader;
  using oxygen::serio::RegionReadStream;

  content.VerifyStorage(check);
  std::unordered_map<data::AssetKey, std::unique_ptr<data::GeometryAsset>>
    geometries;
  std::vector<std::unique_ptr<data::SceneAsset>> scenes;

  const auto rows = [&content](const data::ResourceKind kind) -> uint64_t {
    return content.ResourceTable(kind).RowCount();
  };
  const data::ResourceTableCounts table_counts {
    .buffers = rows(data::ResourceKind::kBuffer),
    .textures = rows(data::ResourceKind::kTexture),
    .scripts = rows(data::ResourceKind::kScript),
    .physics = rows(data::ResourceKind::kPhysics),
  };

  for (const auto& asset : content.Assets()) {
    const auto descriptor_name = asset.virtual_path.empty()
      ? nostd::to_string(asset.key)
      : asset.virtual_path;
    const auto descriptor = content.Descriptor(asset.key);
    const auto descriptor_size = descriptor.size();
    auto file = MemoryFile::FromBytes(descriptor);
    if (!file) {
      throw Refusal(
        data::CookedDefect::kTruncated, descriptor_name, "cannot be read");
    }
    auto stream = oxygen::serio::OpenReadStream(*file);
    if (!stream) {
      throw Refusal(
        data::CookedDefect::kTruncated, descriptor_name, "cannot be read");
    }

    if (descriptor_size < sizeof(AssetHeader)) {
      throw Refusal(data::CookedDefect::kTooSmall, descriptor_name,
        "is smaller than its asset header");
    }

    Reader<RegionReadStream<MemoryFile>> reader(*stream);
    auto pack = reader.ScopedAlignment(1);
    auto blob = reader.ReadBlob(sizeof(AssetHeader));
    if (!blob) {
      throw Refusal(data::CookedDefect::kTruncated, descriptor_name,
        "has an unreadable asset header");
    }

    const auto header_read
      = serio::CopyAt<AssetHeader>(std::as_bytes(std::span { *blob }));
    if (!header_read) {
      throw Refusal(data::CookedDefect::kTruncated, descriptor_name,
        "has an unreadable asset header");
    }
    const auto& header = *header_read;

    if (static_cast<AssetType>(header.asset_type) != asset.type) {
      throw Refusal(data::CookedDefect::kInconsistent, descriptor_name,
        "has another asset type than its source records");
    }

    const auto asset_type = asset.type;
    const auto current_version = data::pak::CurrentAssetVersion(asset_type);
    if (!current_version || header.version != *current_version) {
      throw Refusal(data::CookedDefect::kUnsupportedVersion, descriptor_name,
        "has a version this build does not read; re-cook it with the current "
        "tools");
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
      throw Refusal(data::CookedDefect::kTooSmall, descriptor_name,
        "is smaller than its asset type's descriptor");
    }
    if (!data::DescriptorStrings::IsValidLocation(
          header.strings, descriptor_size)
      || header.strings.offset < min_size) {
      throw Refusal(data::CookedDefect::kOutOfBounds, descriptor_name,
        "has a string table outside it");
    }

    const auto& references = asset.references;
    if (const auto valid = references.ValidateResourceBounds(table_counts);
      !valid) {
      throw Refusal(
        data::CookedDefect::kInvalidReference, descriptor_name, valid.error());
    }
    if (asset_type == AssetType::kScene || asset_type == AssetType::kMaterial
      || asset_type == AssetType::kGeometry) {
      if (!reader.Seek(0)) {
        throw Refusal(
          data::CookedDefect::kTruncated, descriptor_name, "cannot be read");
      }
      try {
        // Runtime parse-only loaders own descriptor versions and value checks.
        // Unmounted external dependencies do not invalidate descriptor data.
        const LoaderContext context {
          .current_asset_key = asset.key,
          .desc_reader = &reader,
          .asset_references = observer_ptr(&references),
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
          throw Refusal(data::CookedDefect::kSizeMismatch, descriptor_name,
            "holds bytes after its parsed end");
        }
      } catch (const data::CookedFormatError& error) {
        // The loaders name no descriptor; the refusal keeps their defect.
        throw Refusal(error.Defect(), descriptor_name, error.what());
      }
    } else {
      if (!reader.Seek(0)) {
        throw Refusal(
          data::CookedDefect::kTruncated, descriptor_name, "cannot be read");
      }
      const auto bytes = reader.ReadBlob(descriptor_size);
      if (!bytes) {
        throw Refusal(
          data::CookedDefect::kTruncated, descriptor_name, "cannot be read");
      }
      ValidateAssetDescriptor(asset_type, asset.key, *bytes, references);
    }
  }

  for (const auto& scene : scenes) {
    std::unordered_map<data::pak::world::SceneNodeIndexT, data::AssetKey>
      geometry_by_node;
    for (const auto& renderable :
      scene->GetComponents<data::pak::world::RenderableRecord>()) {
      geometry_by_node.emplace(renderable.node_index, renderable.geometry_key);
    }
    for (const auto& record :
      scene->GetComponents<data::pak::world::OverrideRecord>()) {
      const auto* property
        = data::schema::FindOverrideProperty(record.override_id);
      // The scene validated every override against the schema when it loaded.
      DCHECK_NOTNULL_F(property);
      // A change of an instanced node names a surface of the instanced
      // scene's geometry, which composition resolves.
      if (property->part_kind != data::schema::PartKind::kSurface
        || record.target.node_path != data::pak::world::kNoNodePath) {
        continue;
      }
      const auto node = geometry_by_node.find(record.target.node_index);
      if (node == geometry_by_node.end()) {
        throw Refusal(data::CookedDefect::kInconsistent,
          nostd::to_string(scene->GetAssetKey()),
          "overrides a surface of a node without a renderable");
      }
      const auto found = geometries.find(node->second);
      if (found == geometries.end()) {
        continue; // Other mounted roots are outside this root's inventory.
      }
      const auto& geometry = *found->second;
      if (geometry.FindSurface(record.part_id) == nullptr) {
        throw Refusal(data::CookedDefect::kInconsistent,
          nostd::to_string(scene->GetAssetKey()),
          "overrides the unknown surface " + data::to_string(record.part_id));
      }
      if (geometry.Surfaces().layout_revision != record.layout_revision) {
        throw Refusal(data::CookedDefect::kInconsistent,
          nostd::to_string(scene->GetAssetKey()),
          "overrides a surface of an older layout; it requires repair");
      }
    }
  }
}

} // namespace oxygen::content::cooked
