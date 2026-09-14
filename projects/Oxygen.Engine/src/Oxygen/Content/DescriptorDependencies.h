//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <vector>

#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::serio {
class AnyReader;
}

namespace oxygen::content {

//! Direct asset-key references decoded without loading buffers or other assets.
struct DescriptorDependencies final {
  std::vector<data::AssetKey> assets;
  bool complete = true;
  std::string explanation;
};

//! Inspect a descriptor with the runtime's structural decoders in parse-only
//! mode.
/*!
 @param reader Reader positioned at the start of the descriptor.
 @param key Asset identity recorded in the owning container.
 @param type Asset type recorded in the owning container.
 @return Direct references and whether all asset dependencies were inspected.
 @throw std::runtime_error If a supported descriptor is malformed or mismatched.
*/
OXGN_CNTT_NDAPI auto InspectDescriptorDependencies(serio::AnyReader& reader,
  const data::AssetKey& key, data::AssetType type) -> DescriptorDependencies;

} // namespace oxygen::content
