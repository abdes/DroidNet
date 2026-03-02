//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstring>
#include <iostream>

#include <fmt/format.h>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/PakFormat.h>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"

namespace oxygen::content::pakdump {

//! Dumps collision shape asset descriptors.
class CollisionShapeAssetDumper final : public AssetDumper {
public:
  auto DumpAsync(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::core::AssetDirectoryEntry& entry, DumpContext& ctx,
    const size_t idx, oxygen::content::AssetLoader& asset_loader) const
    -> oxygen::co::Co<> override
  {
    (void)asset_loader;

    using oxygen::data::pak::physics::CollisionShapeAssetDesc;

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
    PrintUtils::Field("Shape Type", static_cast<uint32_t>(shape.shape_type), 8);
    PrintUtils::Field("Local Position",
      fmt::format("[{:.3f}, {:.3f}, {:.3f}]", shape.local_position[0],
        shape.local_position[1], shape.local_position[2]),
      8);
    PrintUtils::Field("Local Rotation",
      fmt::format("[{:.3f}, {:.3f}, {:.3f}, {:.3f}]", shape.local_rotation[0],
        shape.local_rotation[1], shape.local_rotation[2],
        shape.local_rotation[3]),
      8);
    PrintUtils::Field("Local Scale",
      fmt::format("[{:.3f}, {:.3f}, {:.3f}]", shape.local_scale[0],
        shape.local_scale[1], shape.local_scale[2]),
      8);
    PrintUtils::Field("Is Sensor", shape.is_sensor, 8);
    PrintUtils::Field("Own Layer", shape.collision_own_layer, 8);
    PrintUtils::Field("Target Layers", shape.collision_target_layers, 8);
    PrintUtils::Field(
      "Material Key", oxygen::data::to_string(shape.material_asset_key), 8);
    PrintUtils::Field(
      "Cooked Ref Index", shape.cooked_shape_ref.resource_index, 8);
    PrintUtils::Field("Cooked Ref Type",
      static_cast<uint32_t>(shape.cooked_shape_ref.payload_type), 8);
    std::cout << "\n";

    co_return;
  }
};

} // namespace oxygen::content::pakdump
