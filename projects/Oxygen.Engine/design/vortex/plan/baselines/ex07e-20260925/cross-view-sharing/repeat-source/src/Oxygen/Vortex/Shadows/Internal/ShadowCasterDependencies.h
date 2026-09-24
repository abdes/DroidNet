//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <unordered_map>
#include <vector>

#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::shadows::internal {

//! Interns by exact equality after hash lookup; weak entries never own casters.
class ShadowCasterDependencies final {
public:
  OXGN_VRTX_API auto Build(const PreparedSceneFrame& scene,
    std::vector<ShadowCasterDependency>& output) -> void;
  OXGN_VRTX_API auto Intern(ShadowCasterRecord record, std::uint64_t hash)
    -> std::shared_ptr<const ShadowCasterRecord>;
  OXGN_VRTX_API auto Prune() -> void;

private:
  std::vector<ShadowCasterDependency> scratch_;
  std::unordered_multimap<std::uint64_t,
    std::weak_ptr<const ShadowCasterRecord>>
    records_;
};

} // namespace oxygen::vortex::shadows::internal
