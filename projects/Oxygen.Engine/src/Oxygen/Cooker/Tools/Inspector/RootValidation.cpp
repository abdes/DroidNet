//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "RootValidation.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <ios>
#include <stdexcept>

#include <Oxygen/Content/DescriptorDependencies.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::inspection {

auto ValidateRootOrThrow(const std::filesystem::path& cooked_root) -> void
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

  for (const auto& asset : inspection.Assets()) {
    if (asset.descriptor_relpath.empty()) {
      throw std::runtime_error("asset descriptor path is missing");
    }

    const auto descriptor_path = cooked_root / asset.descriptor_relpath;
    if (!std::filesystem::exists(descriptor_path)) {
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

    if (asset_type == AssetType::kScene) {
      if (!reader.Seek(0)) {
        throw std::runtime_error("failed to rewind scene descriptor: "
          + descriptor_path.generic_string());
      }
      try {
        // The runtime parse-only loader owns scene versions, node records and
        // source-mode validation. Incomplete external script dependencies do
        // not make an otherwise valid descriptor invalid.
        static_cast<void>(
          InspectDescriptorDependencies(reader, asset.key, asset_type));
      } catch (const std::exception& error) {
        throw std::runtime_error("invalid scene descriptor '"
          + descriptor_path.generic_string() + "': " + error.what());
      }
    }
  }
}

} // namespace oxygen::content::inspection
