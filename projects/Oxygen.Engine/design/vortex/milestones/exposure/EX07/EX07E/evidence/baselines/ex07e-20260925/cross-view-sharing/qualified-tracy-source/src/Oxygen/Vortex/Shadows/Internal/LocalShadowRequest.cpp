//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#include <algorithm>
#include <cmath>
#include <functional>

#include <glm/gtc/matrix_access.hpp>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Core/Types/Frustum.h>
#include <Oxygen/Vortex/Shadows/Internal/LocalShadowProjection.h>
#include <Oxygen/Vortex/Shadows/Internal/LocalShadowRequest.h>
#include <Oxygen/Vortex/Shadows/Internal/PointShadowSetup.h>
#include <Oxygen/Vortex/Shadows/Internal/SpotShadowSetup.h>

namespace oxygen::vortex::shadows::internal {
auto LocalShadowContentKey::operator==(const LocalShadowContentKey& other) const
  -> bool
{
  if (scene_generation != other.scene_generation || light != other.light
    || resolution != other.resolution || format != other.format
    || contract != other.contract || matrices != other.matrices
    || bias != other.bias || direction != other.direction
    || position_and_inverse_range != other.position_and_inverse_range
    || casters.size() != other.casters.size()) {
    return false;
  }
  // Both sets are canonicalized by the same service interner. Compare values
  // on a pointer mismatch: hashes alone never establish content equivalence.
  for (size_t i = 0; i < casters.size(); ++i) {
    if (casters[i] != other.casters[i] && *casters[i] != *other.casters[i]) {
      return false;
    }
  }
  return true;
}
auto PrepareLocalShadowRequest(const PreparedViewShadowInput& view,
  const FrameLocalLightSelection& light, LightSelectionIndex selection,
  std::uint32_t resolution, float strength, LocalShadowRequest& request) -> void
{
  request.selection = selection;
  request.strength = strength;
  auto& key = request.content;
  key.casters.clear();
  key.matrices.fill(glm::mat4 { 0 });
  key.bias = key.direction = glm::vec4 { 0 };
  key.contract = LocalShadowDepthContract::kProjectedLinearReversedV1;
  key.scene_generation = view.scene_generation;
  key.light = light.source_node;
  key.resolution = resolution;
  key.position_and_inverse_range
    = glm::vec4(light.position, 1.0F / light.range);
  const auto cube = UsesCubeLocalShadow(light);
  if (cube) {
    auto record = PointShadowSetup::PreparePointProjection(light, resolution);
    key.contract = LocalShadowDepthContract::kCubeRasterReversedV1;
    key.matrices = record.face_light_view_projection;
    request.projection = record;
  } else {
    auto record = SpotShadowSetup::PrepareSpotProjection(light, resolution);
    key.matrices[0] = record.light_view_projection;
    key.bias = glm::vec4(record.depth_bias,
      record.depth_bias * kLocalShadowSlopeDepthBiasScale, 1.0F, 0.0F);
    key.direction
      = glm::vec4(glm::vec3(glm::row(record.light_view_projection, 3)), 0.0F);
    request.projection = record;
  }
  key.reusable = view.shadow_dependencies_available
    && view.scene_generation != 0 && light.source_node.IsValid()
    && resolution != 0 && std::isfinite(light.range) && light.range > 0;
  const auto frustum = Frustum::FromViewProj(key.matrices[0], true);
  auto& hashes = request.caster_hash_scratch;
  hashes.clear();
  for (const auto& caster : view.shadow_caster_dependencies) {
    const auto bounds = caster.bounds;
    if (std::isfinite(bounds.x) && std::isfinite(bounds.y)
      && std::isfinite(bounds.z) && std::isfinite(bounds.w) && bounds.w > 0) {
      const auto radius = bounds.w * 1.01F + 1.0e-4F;
      const auto delta = glm::vec3(bounds) - light.position;
      const auto extent = radius + light.range;
      if (glm::dot(delta, delta) > extent * extent
        || (!cube && !frustum.IntersectsSphere(glm::vec3(bounds), radius))) {
        continue;
      }
    }
    if (!caster.reusable || !caster.record) {
      key.reusable = false;
    }
    if (caster.record) {
      key.casters.push_back(caster.record);
    }
    hashes.push_back(caster.fingerprint);
  }
  std::ranges::sort(
    key.casters, {}, [](const auto& record) { return record.get(); });
  std::ranges::sort(hashes);
  std::size_t hash = 0;
  HashCombine(hash, key.scene_generation);
  HashCombine(hash, key.light);
  HashCombine(hash, key.resolution);
  HashCombine(hash, static_cast<unsigned>(key.format));
  HashCombine(hash, static_cast<unsigned>(key.contract));
  HashCombine(hash, ComputeFNV1a64(key.matrices.data(), sizeof(key.matrices)));
  HashCombine(hash, ComputeFNV1a64(&key.bias, sizeof(key.bias)));
  HashCombine(hash, ComputeFNV1a64(&key.direction, sizeof(key.direction)));
  HashCombine(hash,
    ComputeFNV1a64(
      &key.position_and_inverse_range, sizeof(key.position_and_inverse_range)));
  HashCombine(
    hash, ComputeFNV1a64(hashes.data(), hashes.size() * sizeof(hashes[0])));
  key.hash = hash;
}
}
