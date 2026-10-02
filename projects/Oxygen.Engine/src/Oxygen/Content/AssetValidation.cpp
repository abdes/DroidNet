//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <stdexcept>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetValidation.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/InputActionLoader.h>
#include <Oxygen/Content/Loaders/InputMappingContextLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/PhysicsAssetLoader.h>
#include <Oxygen/Content/Loaders/PhysicsSceneLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/Loaders/ScriptLoader.h>
#include <Oxygen/Data/PakFormatVersions.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content {

auto ValidateAssetDescriptor(const data::AssetType type,
  const data::AssetKey& key, const std::span<const std::byte> bytes,
  const data::AssetReferences& references) -> void
{
  data::pak::core::AssetHeader header {};
  if (bytes.size() < sizeof(header)) {
    throw std::runtime_error("Asset descriptor is smaller than its header");
  }
  std::memcpy(&header, bytes.data(), sizeof(header));
  const auto version = data::pak::CurrentAssetVersion(type);
  if (!version || header.version != *version
    || header.asset_type != static_cast<uint8_t>(type)) {
    throw std::runtime_error("Asset descriptor type/version is invalid");
  }
  serio::ReadOnlyMemoryStream stream(bytes);
  serio::Reader reader(stream);
  const LoaderContext context {
    .current_asset_key = key,
    .desc_reader = &reader,
    .asset_references = observer_ptr(&references),
    .work_offline = true,
    .parse_only = true,
  };
  switch (type) {
  case data::AssetType::kMaterial:
    static_cast<void>(loaders::LoadMaterialAsset(context));
    break;
  case data::AssetType::kGeometry:
    static_cast<void>(loaders::LoadGeometryAsset(context));
    break;
  case data::AssetType::kScene:
    static_cast<void>(loaders::LoadSceneAsset(context));
    break;
  case data::AssetType::kScript:
    static_cast<void>(loaders::LoadScriptAsset(context));
    break;
  case data::AssetType::kInputAction:
    static_cast<void>(loaders::LoadInputActionAsset(context));
    break;
  case data::AssetType::kInputMappingContext:
    static_cast<void>(loaders::LoadInputMappingContextAsset(context));
    break;
  case data::AssetType::kPhysicsScene:
    static_cast<void>(loaders::LoadPhysicsSceneAsset(context));
    break;
  case data::AssetType::kPhysicsMaterial:
    static_cast<void>(loaders::LoadPhysicsMaterialDescriptor(context));
    break;
  case data::AssetType::kCollisionShape:
    static_cast<void>(loaders::LoadCollisionShapeDescriptor(context));
    break;
  default:
    throw std::runtime_error(
      "Unsupported asset type for descriptor validation");
  }
  const auto consumed = reader.Position();
  if (!consumed || *consumed != bytes.size()) {
    throw std::runtime_error(
      "Asset descriptor contains unexpected trailing bytes");
  }
}

} // namespace oxygen::content
