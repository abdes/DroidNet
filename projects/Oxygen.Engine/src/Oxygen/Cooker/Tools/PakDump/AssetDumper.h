//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>

#include "DumpContext.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content {
class AssetLoader;
class PakFile;
} // namespace oxygen::content

namespace oxygen::data::pak::core {
struct AssetDirectoryEntry;
} // namespace oxygen::data::pak::core

namespace oxygen::content::pakdump {

//! Asset descriptor dumper interface.
class AssetDumper {
public:
  AssetDumper() = default;
  virtual ~AssetDumper() = default;
  OXYGEN_MAKE_NON_COPYABLE(AssetDumper)
  OXYGEN_MAKE_NON_MOVABLE(AssetDumper)

  virtual auto DumpAsync(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::core::AssetDirectoryEntry& entry, DumpContext& ctx,
    size_t idx, oxygen::content::AssetLoader& asset_loader) const
    -> oxygen::co::Co<> = 0;
};

} // namespace oxygen::content::pakdump
