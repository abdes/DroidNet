//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Vortex/Lighting/Internal/DeferredLightPacketBuilder.h>
#include <Oxygen/Vortex/Lighting/Internal/LightEvaluationRecords.h>
#include <Oxygen/Vortex/Lighting/Types/DirectionalLightForwardData.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>

namespace oxygen::vortex::lighting::internal {

namespace {

  auto MakeScaleMatrix(const glm::vec3 scale) -> glm::mat4
  {
    return glm::scale(glm::mat4 { 1.0F }, scale);
  }

  auto NormalizeOrFallback(const glm::vec3 direction, const glm::vec3 fallback)
    -> glm::vec3
  {
    const auto length_sq = glm::dot(direction, direction);
    if (length_sq <= oxygen::math::EpsilonDirection) {
      return fallback;
    }
    return direction / std::sqrt(length_sq);
  }

  auto RotationFromDirToDir(const glm::vec3& from_dir,
    const glm::vec3& fallback_dir, const glm::vec3& up_axis,
    const glm::vec3& direction) -> glm::quat
  {
    const auto to_dir = NormalizeOrFallback(direction, fallback_dir);
    const auto cos_theta = std::clamp(glm::dot(from_dir, to_dir), -1.0F, 1.0F);

    if (cos_theta >= 0.9999F) {
      return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
    }

    if (cos_theta <= -0.9999F) {
      return glm::angleAxis(oxygen::math::Pi, up_axis);
    }

    const auto axis = glm::normalize(glm::cross(from_dir, to_dir));
    const auto angle = std::acos(cos_theta);
    return glm::angleAxis(angle, axis);
  }

  auto BuildLightWorldMatrix(const FrameLocalLightSelection& selection,
    const bool spherical_proxy) -> glm::mat4
  {
    const auto translation
      = glm::translate(glm::mat4 { 1.0F }, selection.position);
    if (spherical_proxy) {
      return translation
        * MakeScaleMatrix(
          glm::vec3 { selection.range + selection.source_radius });
    }

    const auto outer_cosine = std::clamp(
      std::cos(selection.outer_cone_half_angle_radians), 0.001F, 0.999999F);
    const auto outer_sine
      = std::sqrt((std::max)(1.0F - outer_cosine * outer_cosine, 0.0F));
    const auto outer_tangent = outer_sine / (std::max)(outer_cosine, 1.0e-4F);
    const auto base_radius
      = (std::max)(selection.range * outer_tangent, 0.001F);
    const auto rotation = glm::mat4_cast(RotationFromDirToDir(
      oxygen::space::move::Forward, oxygen::space::move::Forward,
      oxygen::space::move::Up, selection.direction));
    return translation * rotation
      * MakeScaleMatrix(
        glm::vec3 { base_radius, selection.range, base_radius });
  }

} // namespace

auto DeferredLightPacketBuilder::Build(const FrameLightSelection& selection,
  const LightEvaluationRecords& evaluation) const -> DeferredLightPacketSet
{
  auto packets
    = DeferredLightPacketSet { .selection_epoch = selection.selection_epoch };
  packets.directional = evaluation.directional;
  packets.local_lights.reserve(evaluation.local.size());
  for (std::size_t index = 0; index < evaluation.local.size(); ++index) {
    const auto& source = selection.local_lights.at(index);
    const auto& light = evaluation.local.at(index);
    if (light.range_m == 0.0F) {
      continue;
    }
    const auto spherical = source.kind == LocalLightKind::kPoint
      || source.source_radius > 0.0F
      || source.outer_cone_half_angle_radians
        == std::numbers::pi_v<float> / 2.0F;
    packets.local_lights.push_back(DeferredLightPacket {
      .kind = source.kind,
      .light = observer_ptr { &light },
      .light_world_matrix = BuildLightWorldMatrix(source, spherical),
      .spherical_proxy = spherical,
    });
  }
  return packets;
}

} // namespace oxygen::vortex::lighting::internal
