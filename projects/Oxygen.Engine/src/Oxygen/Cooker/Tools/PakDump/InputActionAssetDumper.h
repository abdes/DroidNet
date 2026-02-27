//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstring>
#include <iostream>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Data/PakFormat.h>

#include "AssetDumpHelpers.h"
#include "AssetDumper.h"
#include "PrintUtils.h"

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

    std::cout << "Asset #" << idx << ":\n";
    asset_dump_helpers::PrintAssetKey(entry.asset_key, ctx);
    asset_dump_helpers::PrintAssetMetadata(entry);

    const auto data = asset_dump_helpers::ReadDescriptorBytes(pak, entry);
    if (!data) {
      std::cout << "    Failed to read asset descriptor data\n\n";
      co_return;
    }

    asset_dump_helpers::PrintAssetDescriptorHexPreview(*data, ctx);
    if (data->size() < sizeof(InputActionAssetDesc)) {
      std::cout << "    InputActionAssetDesc: (insufficient data)\n\n";
      co_return;
    }

    InputActionAssetDesc desc {};
    std::memcpy(&desc, data->data(), sizeof(desc));

    asset_dump_helpers::PrintAssetHeaderFields(desc.header, 4);
    std::cout << "    --- Input Action Descriptor Fields ---\n";
    PrintUtils::Field("Value Type", static_cast<uint32_t>(desc.value_type), 8);
    PrintUtils::Field("Flags", nostd::to_string(desc.flags), 8);

    std::cout << "\n";
    co_return;
  }
};

} // namespace oxygen::content::pakdump
