//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Lighting/Internal/DeferredLightProxyGeometry.h>

#include <cmath>
#include <cstdint>
#include <numbers>

namespace oxygen::vortex::lighting::internal {

auto GeneratePointLightProxySphereVertices() -> std::vector<glm::vec4>
{
  constexpr auto kSlices = 16U;
  constexpr auto kStacks = 8U;
  constexpr auto kTwoPi = std::numbers::pi_v<float> * 2.0F;

  const auto ring_vertex
    = [](const std::uint32_t ring, const std::uint32_t slice) -> glm::vec4 {
    const auto phi = std::numbers::pi_v<float> * static_cast<float>(ring)
      / static_cast<float>(kStacks);
    const auto theta
      = kTwoPi * static_cast<float>(slice) / static_cast<float>(kSlices);
    const auto sin_phi = std::sin(phi);
    return glm::vec4(sin_phi * std::cos(theta), std::cos(phi),
      sin_phi * std::sin(theta), 1.0F);
  };

  auto vertices = std::vector<glm::vec4> {};
  vertices.reserve(6U * kSlices * (kStacks - 1U));
  const auto north = glm::vec4(0.0F, 1.0F, 0.0F, 1.0F);
  const auto south = glm::vec4(0.0F, -1.0F, 0.0F, 1.0F);

  for (std::uint32_t slice = 0U; slice < kSlices; ++slice) {
    const auto next_slice = (slice + 1U) % kSlices;
    vertices.push_back(north);
    vertices.push_back(ring_vertex(1U, next_slice));
    vertices.push_back(ring_vertex(1U, slice));
  }

  for (std::uint32_t ring = 1U; ring < kStacks - 1U; ++ring) {
    for (std::uint32_t slice = 0U; slice < kSlices; ++slice) {
      const auto next_slice = (slice + 1U) % kSlices;
      const auto v00 = ring_vertex(ring, slice);
      const auto v01 = ring_vertex(ring, next_slice);
      const auto v10 = ring_vertex(ring + 1U, slice);
      const auto v11 = ring_vertex(ring + 1U, next_slice);
      vertices.insert(vertices.end(), { v00, v01, v10, v10, v01, v11 });
    }
  }

  for (std::uint32_t slice = 0U; slice < kSlices; ++slice) {
    const auto next_slice = (slice + 1U) % kSlices;
    vertices.push_back(south);
    vertices.push_back(ring_vertex(kStacks - 1U, slice));
    vertices.push_back(ring_vertex(kStacks - 1U, next_slice));
  }

  return vertices;
}

auto GenerateSpotLightProxyConeVertices() -> std::vector<glm::vec4>
{
  constexpr auto kSlices = 24U;
  constexpr auto kTwoPi = std::numbers::pi_v<float> * 2.0F;
  const auto ring_vertex = [](const std::uint32_t slice) -> glm::vec4 {
    const auto theta
      = kTwoPi * static_cast<float>(slice) / static_cast<float>(kSlices);
    return glm::vec4(std::cos(theta), -1.0F, std::sin(theta), 1.0F);
  };

  auto vertices = std::vector<glm::vec4> {};
  vertices.reserve(6U * kSlices);
  const auto apex = glm::vec4(0.0F, 0.0F, 0.0F, 1.0F);
  const auto base_center = glm::vec4(0.0F, -1.0F, 0.0F, 1.0F);
  for (std::uint32_t slice = 0U; slice < kSlices; ++slice) {
    const auto next_slice = (slice + 1U) % kSlices;
    vertices.insert(
      vertices.end(), { apex, ring_vertex(next_slice), ring_vertex(slice) });
  }
  for (std::uint32_t slice = 0U; slice < kSlices; ++slice) {
    const auto next_slice = (slice + 1U) % kSlices;
    vertices.insert(vertices.end(),
      { base_center, ring_vertex(slice), ring_vertex(next_slice) });
  }
  return vertices;
}

} // namespace oxygen::vortex::lighting::internal
