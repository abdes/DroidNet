//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstring>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"
#include "PrintUtils.h"

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::pakdump {

class InputActionAssetDumper final : public AssetDumper {
public:
  auto DumpAsync(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::core::AssetDirectoryEntry& entry, DumpContext& ctx,
    const size_t idx, oxygen::content::AssetLoader& asset_loader) const
    -> oxygen::co::Co<> override
  {
    (void)asset_loader;
    using oxygen::data::pak::input::InputActionAssetDesc;

    ctx.Out() << "Asset #" << idx << ":\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(ctx.Out(), entry);

    const auto data = asset_dump_helpers::ReadDescriptorBytes(pak, entry);
    if (!data) {
      ctx.Out() << "    Failed to read asset descriptor data\n\n";
      co_return;
    }

    asset_dump_helpers::PrintAssetDescriptorHexPreview(*data, ctx);
    if (data->size() < sizeof(InputActionAssetDesc)) {
      ctx.Out() << "    InputActionAssetDesc: (insufficient data)\n\n";
      co_return;
    }

    InputActionAssetDesc desc {};
    std::memcpy(&desc, data->data(), sizeof(desc));

    asset_dump_helpers::PrintAssetHeaderFields(ctx.Out(), desc.header,
      asset_dump_helpers::DescriptorString(
        *data, desc.header, desc.header.name_offset),
      4);
    ctx.Out() << "    --- Input Action Descriptor Fields ---\n";
    PrintUtils::Field(
      ctx.Out(), "Value Type", static_cast<uint32_t>(desc.value_type), 8);
    PrintUtils::Field(ctx.Out(), "Flags", nostd::to_string(desc.flags), 8);

    ctx.Out() << "\n";
    co_return;
  }
};

} // namespace oxygen::content::pakdump
