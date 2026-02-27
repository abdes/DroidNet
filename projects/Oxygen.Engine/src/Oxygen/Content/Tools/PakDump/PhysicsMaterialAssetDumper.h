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

//! Dumps physics material asset descriptors.
class PhysicsMaterialAssetDumper final : public AssetDumper {
public:
  auto DumpAsync(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::core::AssetDirectoryEntry& entry, DumpContext& ctx,
    const size_t idx, oxygen::content::AssetLoader& asset_loader) const
    -> oxygen::co::Co<> override
  {
    (void)asset_loader;

    using oxygen::data::pak::physics::PhysicsMaterialAssetDesc;

    std::cout << "Asset #" << idx << " (PhysicsMaterial):\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(entry);

    const auto data = asset_dump_helpers::ReadDescriptorBytes(pak, entry);
    if (!data) {
      std::cout << "    Failed to read asset descriptor data\n\n";
      co_return;
    }

    if (data->size() < sizeof(PhysicsMaterialAssetDesc)) {
      std::cout << "    PhysicsMaterialAssetDesc: (insufficient data)\n\n";
      co_return;
    }

    PhysicsMaterialAssetDesc mat {};
    std::memcpy(&mat, data->data(), sizeof(mat));

    asset_dump_helpers::PrintAssetHeaderFields(mat.header, 4);

    std::cout << "    --- Physics Material Fields ---\n";
    PrintUtils::Field("Friction", mat.friction, 8);
    PrintUtils::Field("Restitution", mat.restitution, 8);
    PrintUtils::Field("Density", mat.density, 8);
    PrintUtils::Field(
      "Friction Combine", static_cast<int>(mat.combine_mode_friction), 8);
    PrintUtils::Field(
      "Restitution Combine", static_cast<int>(mat.combine_mode_restitution), 8);
    std::cout << "\n";

    co_return;
  }
};

} // namespace oxygen::content::pakdump
