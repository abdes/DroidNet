//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/Internal/SourceLayoutHash.h>

namespace oxygen::content::import::test {

//! Fixture geometry is authored directly in these arrays, before producer
//! edits.
[[nodiscard]] inline auto FixtureSourceLayoutWitness(
  const std::span<const MeshLod> lods) -> base::Sha256Digest
{
  SourceLayoutHash hash("oxygen.fixture-source-layout/v1");
  hash.AddCount(lods.size());
  for (const auto& lod : lods) {
    const auto& source = lod.source;
    hash.AddCount(source.streams.positions.size());
    for (const auto& position : source.streams.positions) {
      hash.AddFloat(position.x);
      hash.AddFloat(position.y);
      hash.AddFloat(position.z);
    }
    hash.AddCount(source.indices.size());
    for (const auto index : source.indices) {
      hash.Add(index);
    }
    hash.AddCount(source.ranges.size());
    for (const auto& range : source.ranges) {
      hash.Add(range.source_slot);
      hash.Add(range.first_index);
      hash.Add(range.index_count);
    }
  }
  return hash.Finish();
}

} // namespace oxygen::content::import::test
