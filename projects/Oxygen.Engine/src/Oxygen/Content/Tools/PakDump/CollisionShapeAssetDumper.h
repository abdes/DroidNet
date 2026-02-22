//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstring>
#include <iostream>

#include <fmt/format.h>

#include <Oxygen/Data/PakFormat.h>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"

namespace oxygen::content::pakdump {

//! Dumps collision shape asset descriptors.
class CollisionShapeAssetDumper final : public AssetDumper {
public:
  auto DumpAsync(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::v2::AssetDirectoryEntry& entry, DumpContext& ctx,
    const size_t idx, oxygen::content::AssetLoader& asset_loader) const
    -> oxygen::co::Co<> override
  {
    (void)asset_loader;

    using oxygen::data::pak::v7::CollisionShapeAssetDesc;

    std::cout << "Asset #" << idx << " (CollisionShape):\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(entry);

    const auto data = asset_dump_helpers::ReadDescriptorBytes(pak, entry);
    if (!data) {
      std::cout << "    Failed to read asset descriptor data\n\n";
      co_return;
    }

    if (data->size() < sizeof(CollisionShapeAssetDesc)) {
      std::cout << "    CollisionShapeAssetDesc: (insufficient data)\n\n";
      co_return;
    }

    CollisionShapeAssetDesc shape {};
    std::memcpy(&shape, data->data(), sizeof(shape));

    asset_dump_helpers::PrintAssetHeaderFields(shape.header, 4);

    std::cout << "    --- Collision Shape Fields ---\n";
    PrintUtils::Field("Category", static_cast<int>(shape.shape_category), 8);
    PrintUtils::Field(
      "Physics Resource Index", shape.physics_resource_index, 8);
    PrintUtils::Field("BBox Min",
      fmt::format("[{:.3f}, {:.3f}, {:.3f}]", shape.bounding_box_min[0],
        shape.bounding_box_min[1], shape.bounding_box_min[2]),
      8);
    PrintUtils::Field("BBox Max",
      fmt::format("[{:.3f}, {:.3f}, {:.3f}]", shape.bounding_box_max[0],
        shape.bounding_box_max[1], shape.bounding_box_max[2]),
      8);
    std::cout << "\n";

    co_return;
  }
};

} // namespace oxygen::content::pakdump
