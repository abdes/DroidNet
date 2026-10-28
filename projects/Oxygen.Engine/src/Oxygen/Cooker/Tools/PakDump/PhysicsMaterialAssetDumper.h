//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstring>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"
#include <fmt/format.h>

#include <Oxygen/Data/PakFormat.h>

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

    ctx.Out() << "Asset #" << idx << " (PhysicsMaterial):\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(ctx.Out(), entry);

    const auto data = asset_dump_helpers::ReadDescriptorBytes(pak, entry);
    if (!data) {
      ctx.Out() << "    Failed to read asset descriptor data\n\n";
      co_return;
    }

    if (data->size() < sizeof(PhysicsMaterialAssetDesc)) {
      ctx.Out() << "    PhysicsMaterialAssetDesc: (insufficient data)\n\n";
      co_return;
    }

    PhysicsMaterialAssetDesc mat {};
    std::memcpy(&mat, data->data(), sizeof(mat));

    asset_dump_helpers::PrintAssetHeaderFields(ctx.Out(), mat.header,
      asset_dump_helpers::DescriptorString(
        *data, mat.header, mat.header.name_offset),
      4);

    ctx.Out() << "    --- Physics Material Fields ---\n";
    PrintUtils::Field(ctx.Out(), "Static Friction", mat.static_friction, 8);
    PrintUtils::Field(ctx.Out(), "Dynamic Friction", mat.dynamic_friction, 8);
    PrintUtils::Field(ctx.Out(), "Restitution", mat.restitution, 8);
    PrintUtils::Field(ctx.Out(), "Density", mat.density, 8);
    PrintUtils::Field(ctx.Out(), "Friction Combine",
      static_cast<int>(mat.combine_mode_friction), 8);
    PrintUtils::Field(ctx.Out(), "Restitution Combine",
      static_cast<int>(mat.combine_mode_restitution), 8);
    ctx.Out() << "\n";

    co_return;
  }
};

} // namespace oxygen::content::pakdump
