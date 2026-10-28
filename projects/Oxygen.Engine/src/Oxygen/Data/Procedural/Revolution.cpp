//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>

#include <glm/ext/vector_double3.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/Procedural/Revolution.h>
#include <Oxygen/Data/Procedural/SurfaceLayout.h>
#include <Oxygen/Data/Vertex.h>

namespace oxygen::data::procedural {

namespace {

  //! The height and radius of a cone's slant, unit length, computed in double
  //! so extreme proportions neither overflow nor lose the normal.
  struct SlantFrame final {
    float normal_radial;
    float normal_axial;
  };

  [[nodiscard]] auto MakeSlantFrame(const AxialExtent& extent) -> SlantFrame
  {
    const auto height = static_cast<double>(extent.top_z) - extent.bottom_z;
    const auto slant = std::hypot(height, static_cast<double>(extent.radius));
    return {
      .normal_radial = static_cast<float>(height / slant),
      .normal_axial = static_cast<float>(extent.radius / slant),
    };
  }

  //! Appends a flat disc at `z` with `face`'s layout (the top or bottom face
  //! layout): one centre vertex and `style.segments` rim vertices.
  auto AppendCap(MeshBuffers& buffers, const AxialExtent& extent,
    const SolidStyle& style, const FaceFrame& face, const float z) -> void
  {
    const auto center = static_cast<uint32_t>(buffers.vertices.size());
    buffers.vertices.push_back(Vertex {
      .position = space::move::Up * z,
      .normal = face.normal,
      .texcoord = { 0.5F, 0.5F },
      .tangent = face.tangent,
      .bitangent = face.bitangent,
      .color = style.color,
    });
    for (uint32_t i = 0; i < style.segments; ++i) {
      const auto ring = RingAt(i, style.segments);
      buffers.vertices.push_back(Vertex {
        .position = (ring.outward * extent.radius) + (space::move::Up * z),
        .normal = face.normal,
        .texcoord = {
          0.5F + (0.5F * glm::dot(ring.outward, face.tangent)),
          0.5F + (0.5F * glm::dot(ring.outward, face.bitangent)),
        },
        .tangent = face.tangent,
        .bitangent = face.bitangent,
        .color = style.color,
      });
    }
    // The rim runs counter-clockwise seen from +Z.
    const auto faces_up = glm::dot(face.normal, space::move::Up) > 0.0F;
    for (uint32_t i = 0; i < style.segments; ++i) {
      const auto current = center + 1U + i;
      const auto next = center + 1U + ((i + 1U) % style.segments);
      buffers.indices.insert(buffers.indices.end(),
        {
          center,
          faces_up ? current : next,
          faces_up ? next : current,
        });
    }
  }

  //! Whether triangle (a, b, c) faces the side its vertex normals point to,
  //! tested in double on the stored float32 positions.
  [[nodiscard]] auto FacesOutward(const std::span<const Vertex> vertices,
    const uint32_t a, const uint32_t b, const uint32_t c) -> bool
  {
    const auto first = glm::dvec3(vertices[a].position);
    const auto second = glm::dvec3(vertices[b].position);
    const auto third = glm::dvec3(vertices[c].position);
    const auto normal = glm::dvec3(
      vertices[a].normal + vertices[b].normal + vertices[c].normal);
    return glm::dot(glm::cross(second - first, third - first), normal) > 0.0;
  }

  //! Whether consecutive rim points stay distinct in float32 storage.
  [[nodiscard]] auto RimIsDistinct(
    const AxialExtent& extent, const SolidStyle& style) -> bool
  {
    for (uint32_t i = 0; i < style.segments; ++i) {
      const auto current = RingAt(i, style.segments).outward * extent.radius;
      const auto next = RingAt(i + 1U, style.segments).outward * extent.radius;
      if (current == next) {
        return false;
      }
    }
    return true;
  }

} // namespace

auto AppendCylinder(MeshBuffers& buffers, const AxialExtent& extent,
  const SolidStyle& style) -> bool
{
  if (!(extent.bottom_z < extent.top_z) || !RimIsDistinct(extent, style)) {
    return false;
  }

  // Side: a bottom and a top vertex per meridian, v = 1 at the bottom.
  const auto side = static_cast<uint32_t>(buffers.vertices.size());
  for (uint32_t i = 0; i <= style.segments; ++i) {
    const auto ring = RingAt(i, style.segments);
    const auto u = GridCoordinate(i, style.segments);
    for (const auto [z, v] : {
           std::pair { extent.bottom_z, 1.0F },
           std::pair { extent.top_z, 0.0F },
         }) {
      buffers.vertices.push_back(Vertex {
        .position = (ring.outward * extent.radius) + (space::move::Up * z),
        .normal = ring.outward,
        .texcoord = { u, v },
        .tangent = ring.tangent,
        .bitangent = space::move::Down,
        .color = style.color,
      });
    }
  }
  for (uint32_t i = 0; i < style.segments; ++i) {
    const auto bottom = side + (2U * i);
    const auto top = bottom + 1U;
    buffers.indices.insert(buffers.indices.end(),
      {
        top,
        bottom,
        top + 2U,
        top + 2U,
        bottom,
        bottom + 2U,
      });
  }

  AppendCap(buffers, extent, style, kTopFace, extent.top_z);
  AppendCap(buffers, extent, style, kBottomFace, extent.bottom_z);
  return true;
}

auto AppendCone(MeshBuffers& buffers, const AxialExtent& extent,
  const SolidStyle& style) -> bool
{
  if (!(extent.bottom_z < extent.top_z) || !RimIsDistinct(extent, style)) {
    return false;
  }

  const auto slant = MakeSlantFrame(extent);
  const auto side_frame = [&slant](const RingFrame& ring) -> MeridianFrame {
    return {
      .normal = (ring.outward * slant.normal_radial)
        + (space::move::Up * slant.normal_axial),
      .bitangent = (ring.outward * slant.normal_axial)
        - (space::move::Up * slant.normal_radial),
    };
  };

  // Base ring, v = 1.
  const auto base = static_cast<uint32_t>(buffers.vertices.size());
  for (uint32_t i = 0; i <= style.segments; ++i) {
    const auto ring = RingAt(i, style.segments);
    const auto frame = side_frame(ring);
    buffers.vertices.push_back(Vertex {
      .position
      = (ring.outward * extent.radius) + (space::move::Up * extent.bottom_z),
      .normal = frame.normal,
      .texcoord = { GridCoordinate(i, style.segments), 1.0F },
      .tangent = ring.tangent,
      .bitangent = frame.bitangent,
      .color = style.color,
    });
  }
  // One apex vertex per segment, at the segment's middle meridian, v = 0.
  const auto apex = static_cast<uint32_t>(buffers.vertices.size());
  for (uint32_t i = 0; i < style.segments; ++i) {
    const auto u
      = (static_cast<double>(i) + 0.5) / static_cast<double>(style.segments);
    const auto ring = RingAtTurn(u);
    const auto frame = side_frame(ring);
    buffers.vertices.push_back(Vertex {
      .position = space::move::Up * extent.top_z,
      .normal = frame.normal,
      .texcoord = { static_cast<float>(u), 0.0F },
      .tangent = ring.tangent,
      .bitangent = frame.bitangent,
      .color = style.color,
    });
  }
  for (uint32_t i = 0; i < style.segments; ++i) {
    buffers.indices.insert(
      buffers.indices.end(), { base + i, base + i + 1U, apex + i });
  }

  AppendCap(buffers, extent, style, kBottomFace, extent.bottom_z);
  return true;
}

auto AppendLatitudeSurface(MeshBuffers& buffers,
  const std::span<const Latitude> latitudes, const uint32_t segments) -> bool
{
  const auto first = static_cast<uint32_t>(buffers.vertices.size());
  const auto last_row = static_cast<uint32_t>(latitudes.size() - 1U);
  const auto row_start = [&](const uint32_t row) -> uint32_t {
    return row == 0U ? first
                     : first + segments + ((row - 1U) * (segments + 1U));
  };
  const auto emit = [&](const Latitude& latitude, const RingFrame& ring,
                      const float u) -> void {
    const auto frame = MeridianAt(ring, latitude.sin_theta, latitude.cos_theta);
    buffers.vertices.push_back(Vertex {
      .position
      = (ring.outward * latitude.ring_radius) + (space::move::Up * latitude.z),
      .normal = frame.normal,
      .texcoord = { u, latitude.v },
      .tangent = ring.tangent,
      .bitangent = frame.bitangent,
      .color = { 1.0F, 1.0F, 1.0F, 1.0F },
    });
  };

  for (uint32_t row = 0; row <= last_row; ++row) {
    const auto& latitude = latitudes[row];
    if (row == 0U || row == last_row) {
      for (uint32_t i = 0; i < segments; ++i) {
        const auto u
          = (static_cast<double>(i) + 0.5) / static_cast<double>(segments);
        emit(latitude, RingAtTurn(u), static_cast<float>(u));
      }
      continue;
    }
    for (uint32_t i = 0; i <= segments; ++i) {
      emit(latitude, RingAt(i, segments), GridCoordinate(i, segments));
    }
  }

  // Rows run down from +Z and u counter-clockwise from +Z, so (upper, lower,
  // upper-next) winds counter-clockwise seen from outside.
  const auto append
    = [&](const uint32_t a, const uint32_t b, const uint32_t c) -> bool {
    if (!FacesOutward(buffers.vertices, a, b, c)) {
      return false;
    }
    buffers.indices.insert(buffers.indices.end(), { a, b, c });
    return true;
  };
  for (uint32_t row = 0; row < last_row; ++row) {
    const auto upper = row_start(row);
    const auto lower = row_start(row + 1U);
    for (uint32_t i = 0; i < segments; ++i) {
      auto ok = true;
      if (row == 0U) {
        ok = append(upper + i, lower + i, lower + i + 1U);
      } else if (row + 1U == last_row) {
        ok = append(upper + i, lower + i, upper + i + 1U);
      } else {
        ok = append(upper + i, lower + i, upper + i + 1U)
          && append(upper + i + 1U, lower + i, lower + i + 1U);
      }
      if (!ok) {
        return false;
      }
    }
  }
  return true;
}

} // namespace oxygen::data::procedural
