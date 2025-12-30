//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <unordered_map>

#include <Oxygen/Data/AssetType.h>

#include "AssetDumper.h"
#include "DumpContext.h"
#include "PrintUtils.h"

#include "DefaultAssetDumper.h"
#include "GeometryAssetDumper.h"
#include "MaterialAssetDumper.h"
#include "SceneAssetDumper.h"

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

  auto Dump(const oxygen::content::PakFile& pak, DumpContext& ctx) const -> void
  {
    if (!ctx.show_directory) {
      return;
    }

    PrintUtils::Separator("ASSET DIRECTORY");
    const auto dir = pak.Directory();
    PrintUtils::Field("Asset Count", dir.size());
    std::cout << "\n";

    for (size_t i = 0; i < dir.size(); ++i) {
      const auto& entry = dir[i];
      registry_.Get(entry.asset_type).Dump(pak, entry, ctx, i);
    }
  }

private:
  const AssetDumperRegistry& registry_;
};

} // namespace oxygen::content::pakdump
