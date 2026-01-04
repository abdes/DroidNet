//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>

#include "DumpContext.h"

namespace oxygen::content {
class PakFile;
} // namespace oxygen::content

namespace oxygen::data::pak {
namespace v1 {
  struct AssetDirectoryEntry;
} // namespace v1
} // namespace oxygen::data::pak

namespace oxygen::content::pakdump {

//! Asset descriptor dumper interface.
class AssetDumper {
public:
  virtual ~AssetDumper() = default;

  virtual void Dump(const oxygen::content::PakFile& pak,
    const oxygen::data::pak::v2::AssetDirectoryEntry& entry, DumpContext& ctx,
    size_t idx) const
    = 0;
};

} // namespace oxygen::content::pakdump
