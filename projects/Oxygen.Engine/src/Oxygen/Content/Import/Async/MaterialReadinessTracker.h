//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Content/Import/Async/Pipelines/MaterialPipeline.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::content::import {

//! Tracks material readiness based on texture dependency completion.
/*!
 Builds a dependency map from material texture source IDs to material indices
 and returns ready materials as textures become available.

 @note This helper is job-scoped and not thread-safe.
*/
class MaterialReadinessTracker final {
public:
  //! Build the tracker from a list of material work items.
  OXGN_CNTT_API explicit MaterialReadinessTracker(
    std::span<const MaterialPipeline::WorkItem> materials);

  //! Materials that were ready immediately (no texture dependencies).
  OXGN_CNTT_NDAPI auto TakeReadyWithoutTextures() -> std::vector<size_t>;

  //! Mark a texture source as ready and return newly-ready materials.
  OXGN_CNTT_NDAPI auto MarkTextureReady(std::string_view source_id)
    -> std::vector<size_t>;

private:
  struct MaterialDep final {
    std::unordered_set<std::string> pending_textures;
    bool emitted = false;
  };

  std::vector<MaterialDep> deps_;
  std::unordered_map<std::string, std::vector<size_t>> texture_to_materials_;
  std::vector<size_t> ready_without_textures_;
};

} // namespace oxygen::content::import
