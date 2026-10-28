//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Cooker/Import/Internal/Pipelines/GeometryPipeline_tangents.h>
#include <Oxygen/Cooker/Import/Internal/mikktspace/mikktspace.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/Vertex.h>

namespace oxygen::content::import::util {

namespace {
  //! The mesh MikkTSpace reads through its callbacks.
  struct MikkMesh final {
    std::vector<data::Vertex>&
      vertices; // NOLINT(*-avoid-const-or-ref-data-members)
    std::vector<uint32_t> triangles;
  };

  auto MeshOf(const SMikkTSpaceContext* context) -> MikkMesh&
  {
    return *static_cast<MikkMesh*>(context->m_pUserData);
  }

  auto CornerOf(const SMikkTSpaceContext* context, const int face,
    const int vertex) -> data::Vertex&
  {
    auto& mesh = MeshOf(context);
    const auto corner
      = (static_cast<size_t>(face) * 3U) + static_cast<size_t>(vertex);
    return mesh.vertices.at(mesh.triangles.at(corner));
  }

  auto Write(float* out, const glm::vec3& value) -> void
  {
    const auto target = std::span<float, 3>(out, 3);
    target[0] = value.x;
    target[1] = value.y;
    target[2] = value.z;
  }

  auto NumFaces(const SMikkTSpaceContext* context) -> int
  {
    return static_cast<int>(MeshOf(context).triangles.size() / 3U);
  }

  auto NumVerticesOfFace(
    const SMikkTSpaceContext* /*context*/, const int /*face*/) -> int
  {
    return 3;
  }

  auto GetPosition(const SMikkTSpaceContext* context, float* out,
    const int face, const int vertex) -> void
  {
    Write(out, CornerOf(context, face, vertex).position);
  }

  auto GetNormal(const SMikkTSpaceContext* context, float* out, const int face,
    const int vertex) -> void
  {
    Write(out, CornerOf(context, face, vertex).normal);
  }

  auto GetTexCoord(const SMikkTSpaceContext* context, float* out,
    const int face, const int vertex) -> void
  {
    const auto& texcoord = CornerOf(context, face, vertex).texcoord;
    const auto target = std::span<float, 2>(out, 2);
    target[0] = texcoord.x;
    target[1] = texcoord.y;
  }

  //! Stores MikkTSpace's frame as `space::texture`'s: the tangent as given,
  //! the bitangent sign * cross(N, T), which runs along +dP/dV.
  // MikkTSpace's C interface fixes this callback's parameters.
  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  auto SetTSpaceBasic(const SMikkTSpaceContext* context, const float* tangent,
    const float sign, const int face, const int vertex) -> void
  // NOLINTEND(bugprone-easily-swappable-parameters)
  {
    auto& corner = CornerOf(context, face, vertex);
    const auto source = std::span<const float, 3>(tangent, 3);
    const auto t = glm::vec3 { source[0], source[1], source[2] };
    const auto length = glm::length(corner.normal);
    const auto n
      = length > 0.0F ? corner.normal / length : glm::vec3 { space::move::Up };
    corner.tangent = t;
    corner.bitangent = sign * glm::cross(n, t);
  }
} // namespace

auto GenerateMikkTSpaceTangents(std::vector<data::Vertex>& vertices,
  const std::span<const uint32_t> triangles) -> void
{
  auto mesh = MikkMesh { .vertices = vertices, .triangles = {} };
  mesh.triangles.reserve(triangles.size());
  for (size_t first = 0; first + 3U <= triangles.size(); first += 3U) {
    const auto corners = triangles.subspan(first, 3U);
    if (corners[0] < vertices.size() && corners[1] < vertices.size()
      && corners[2] < vertices.size()) {
      mesh.triangles.insert(
        mesh.triangles.end(), corners.begin(), corners.end());
    }
  }
  if (mesh.triangles.empty()) {
    return;
  }

  auto callbacks = SMikkTSpaceInterface {
    .m_getNumFaces = NumFaces,
    .m_getNumVerticesOfFace = NumVerticesOfFace,
    .m_getPosition = GetPosition,
    .m_getNormal = GetNormal,
    .m_getTexCoord = GetTexCoord,
    .m_setTSpaceBasic = SetTSpaceBasic,
    .m_setTSpace = nullptr,
  };
  const auto context = SMikkTSpaceContext {
    .m_pInterface = &callbacks,
    .m_pUserData = &mesh,
  };
  // Fails only when it cannot allocate; the frames then stay for
  // FixInvalidTangents to repair.
  static_cast<void>(genTangSpaceDefault(&context));
}

} // namespace oxygen::content::import::util
