//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <span>

#include <Oxygen/Content/Import/Async/Adapters/GeometryAdapterTypes.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::content::import::adapters {

//! Builds geometry pipeline work items from FBX scenes.
class FbxGeometryAdapter final {
public:
  OXGN_CNTT_NDAPI auto BuildWorkItems(const std::filesystem::path& source_path,
    const GeometryAdapterInput& input) const -> GeometryAdapterOutput;

  OXGN_CNTT_NDAPI auto BuildWorkItems(std::span<const std::byte> source_bytes,
    const GeometryAdapterInput& input) const -> GeometryAdapterOutput;
};

} // namespace oxygen::content::import::adapters
