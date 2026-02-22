//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

#include <Oxygen/Data/AssetType.h>

#include <Oxygen/OxCo/Co.h>

#include "AssetDumper.h"
#include "DumpContext.h"
#include "PrintUtils.h"

#include "CollisionShapeAssetDumper.h"
#include "DefaultAssetDumper.h"
#include "GeometryAssetDumper.h"
#include "InputActionAssetDumper.h"
#include "InputMappingContextAssetDumper.h"
#include "MaterialAssetDumper.h"
#include "PhysicsMaterialAssetDumper.h"
#include "PhysicsSceneAssetDumper.h"
#include "SceneAssetDumper.h"
#include "ScriptAssetDumper.h"

namespace oxygen::content {
class PakFile;
}

namespace oxygen::content::pakdump {

//! Registry that maps asset type ids to dumper implementations.
class AssetDumperRegistry {
public:
  AssetDumperRegistry()
    : default_dumper_(std::make_unique<DefaultAssetDumper>())
  {
    Register(static_cast<uint8_t>(oxygen::data::AssetType::kMaterial),
      std::make_unique<MaterialAssetDumper>());
    Register(static_cast<uint8_t>(oxygen::data::AssetType::kGeometry),
      std::make_unique<GeometryAssetDumper>());
    Register(static_cast<uint8_t>(oxygen::data::AssetType::kScene),
      std::make_unique<SceneAssetDumper>());
    Register(static_cast<uint8_t>(oxygen::data::AssetType::kScript),
      std::make_unique<ScriptAssetDumper>());
    Register(static_cast<uint8_t>(oxygen::data::AssetType::kInputAction),
      std::make_unique<InputActionAssetDumper>());
    Register(
      static_cast<uint8_t>(oxygen::data::AssetType::kInputMappingContext),
      std::make_unique<InputMappingContextAssetDumper>());
    Register(static_cast<uint8_t>(oxygen::data::AssetType::kPhysicsMaterial),
      std::make_unique<PhysicsMaterialAssetDumper>());
    Register(static_cast<uint8_t>(oxygen::data::AssetType::kCollisionShape),
      std::make_unique<CollisionShapeAssetDumper>());
    Register(static_cast<uint8_t>(oxygen::data::AssetType::kPhysicsScene),
      std::make_unique<PhysicsSceneAssetDumper>());
  }

  [[nodiscard]] auto Get(const uint8_t asset_type) const -> const AssetDumper&
  {
    const auto it = dumpers_.find(asset_type);
    if (it != dumpers_.end()) {
      return *it->second;
    }
    return *default_dumper_;
  }

private:
  void Register(const uint8_t asset_type, std::unique_ptr<AssetDumper> dumper)
  {
    dumpers_[asset_type] = std::move(dumper);
  }

  std::unordered_map<uint8_t, std::unique_ptr<AssetDumper>> dumpers_;
  std::unique_ptr<AssetDumper> default_dumper_;
};

class AssetDirectoryDumper {
public:
  explicit AssetDirectoryDumper(const AssetDumperRegistry& registry)
    : registry_(registry)
  {
  }

  auto DumpAsync(const oxygen::content::PakFile& pak, DumpContext& ctx,
    oxygen::content::AssetLoader& asset_loader) const -> oxygen::co::Co<>
  {
    if (!ctx.show_directory) {
      co_return;
    }

    PrintUtils::Separator("ASSET DIRECTORY");
    const auto dir = pak.Directory();
    PrintUtils::Field("Asset Count", dir.size());
    std::cout << "\n";

    for (size_t i = 0; i < dir.size(); ++i) {
      const auto& entry = dir[i];
      try {
        co_await registry_.Get(entry.asset_type)
          .DumpAsync(pak, entry, ctx, i, asset_loader);
      } catch (const std::exception& ex) {
        std::cerr << "ERROR: failed to dump asset #" << i
                  << " (type=" << static_cast<int>(entry.asset_type)
                  << "): " << ex.what() << "\n\n";
      } catch (...) {
        std::cerr << "ERROR: failed to dump asset #" << i
                  << " (type=" << static_cast<int>(entry.asset_type)
                  << "): unknown exception\n\n";
      }
    }

    co_return;
  }

private:
  const AssetDumperRegistry& registry_;
};

} // namespace oxygen::content::pakdump
