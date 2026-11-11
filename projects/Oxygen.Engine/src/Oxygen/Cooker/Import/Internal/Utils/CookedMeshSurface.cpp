//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Import/Internal/Utils/CookedMeshSurface.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::content::import::internal {

namespace {

  using Position = std::array<float, 3>;
  using Failure = std::unexpected<std::string>;

  //! Where the first mesh of a geometry descriptor takes its data from.
  struct MeshSource final {
    enum class Kind : uint8_t {
      kStandard,
      kProcedural,
    };

    Kind kind = Kind::kStandard;
    data::pak::core::ResourceIndexT vertex_buffer
      = data::pak::core::kNoResourceIndex;
    data::pak::core::ResourceIndexT index_buffer
      = data::pak::core::kNoResourceIndex;
    std::string procedural_name;
    std::vector<std::byte> procedural_params;
    std::vector<data::pak::geometry::MeshViewDesc> mesh_views;
  };

  //! Reads the procedural name and parameters of `mesh_desc`, which start at
  //! `offset`; advances `offset` past them.
  auto ReadProceduralSource(const std::span<const std::byte> descriptor_bytes,
    const data::pak::geometry::GeometryAssetDesc& asset_desc,
    const data::pak::geometry::MeshDesc& mesh_desc, size_t& offset,
    MeshSource& out) -> std::expected<void, std::string>
  {
    out.kind = MeshSource::Kind::kProcedural;
    try {
      out.procedural_name = std::string {
        data::DescriptorStrings::FromDescriptor(
          descriptor_bytes, asset_desc.header.strings)
          .Get(mesh_desc.name_offset),
      };
    } catch (const std::invalid_argument&) {
      return Failure("geometry descriptor procedural mesh name is invalid");
    }
    const auto params_size
      = static_cast<size_t>(mesh_desc.info.procedural.params_size);
    if (offset > descriptor_bytes.size()
      || descriptor_bytes.size() - offset < params_size) {
      return Failure(
        "geometry descriptor procedural params exceed descriptor size");
    }
    const auto params = descriptor_bytes.subspan(offset, params_size);
    out.procedural_params.assign(params.begin(), params.end());
    offset += params_size;
    return {};
  }

  //! Reads the mesh views of every submesh of the mesh, which start at
  //! `offset`.
  auto ReadMeshViews(const std::span<const std::byte> descriptor_bytes,
    const data::pak::geometry::MeshDesc& mesh_desc, size_t offset,
    MeshSource& out) -> std::expected<void, std::string>
  {
    using data::pak::geometry::MeshViewDesc;
    using data::pak::geometry::SubMeshDesc;

    out.mesh_views.clear();
    for (uint32_t submesh_index = 0; submesh_index < mesh_desc.submesh_count;
      ++submesh_index) {
      const auto submesh_desc = serio::CopyAt<SubMeshDesc>(
        descriptor_bytes, serio::ByteOffset { offset });
      if (!submesh_desc) {
        return Failure("geometry descriptor submesh table exceeds its size");
      }
      offset += sizeof(SubMeshDesc);

      for (uint32_t view_index = 0; view_index < submesh_desc->mesh_view_count;
        ++view_index) {
        const auto view_desc = serio::CopyAt<MeshViewDesc>(
          descriptor_bytes, serio::ByteOffset { offset });
        if (!view_desc) {
          return Failure(
            "geometry descriptor mesh view table exceeds its size");
        }
        offset += sizeof(MeshViewDesc);
        out.mesh_views.push_back(*view_desc);
      }
    }
    if (out.mesh_views.empty()) {
      return Failure("geometry has no mesh views");
    }
    return {};
  }

  //! Reads where the first mesh of the geometry takes its data from.
  auto ReadMeshSource(const std::span<const std::byte> descriptor_bytes,
    const data::AssetReferences& references)
    -> std::expected<MeshSource, std::string>
  {
    using data::MeshType;
    using data::pak::geometry::GeometryAssetDesc;
    using data::pak::geometry::MeshDesc;

    const auto asset_desc = serio::CopyAt<GeometryAssetDesc>(descriptor_bytes);
    if (!asset_desc) {
      return Failure("geometry descriptor is too small for GeometryAssetDesc");
    }
    if (asset_desc->lod_count == 0U) {
      return Failure("geometry descriptor has lod_count=0");
    }

    auto offset = sizeof(GeometryAssetDesc);
    const auto mesh_desc
      = serio::CopyAt<MeshDesc>(descriptor_bytes, serio::ByteOffset { offset });
    if (!mesh_desc) {
      return Failure("geometry descriptor is too small for MeshDesc");
    }
    if (!data::pak::geometry::HasZeroReserves(*mesh_desc)) {
      return Failure("geometry descriptor mesh reserved bytes are not zero");
    }
    offset += sizeof(MeshDesc);

    auto out = MeshSource {};
    switch (static_cast<MeshType>(mesh_desc->mesh_type)) {
    case MeshType::kStandard: {
      const auto vertex = references.ResolveResource(
        mesh_desc->info.standard.vertex_buffer, data::ResourceKind::kBuffer);
      const auto index = references.ResolveResource(
        mesh_desc->info.standard.index_buffer, data::ResourceKind::kBuffer);
      if (!vertex || !index) {
        return Failure("geometry has invalid buffer references");
      }
      out.kind = MeshSource::Kind::kStandard;
      out.vertex_buffer = vertex->value_or(data::pak::core::kNoResourceIndex);
      out.index_buffer = index->value_or(data::pak::core::kNoResourceIndex);
      break;
    }
    case MeshType::kProcedural:
      if (auto read = ReadProceduralSource(
            descriptor_bytes, *asset_desc, *mesh_desc, offset, out);
        !read) {
        return std::unexpected(std::move(read.error()));
      }
      break;
    default:
      return Failure("geometry mesh must be standard or procedural");
    }

    if (auto views = ReadMeshViews(descriptor_bytes, *mesh_desc, offset, out);
      !views) {
      return std::unexpected(std::move(views.error()));
    }
    return out;
  }

  //! Reads row `index` of `buffers` and its payload.
  auto ReadBuffer(const cooked::CookedResourceTable& buffers,
    const data::pak::core::ResourceIndexT index,
    data::pak::core::BufferResourceDesc& descriptor,
    std::vector<std::byte>& payload) -> std::expected<void, std::string>
  {
    if (index == data::pak::core::kNoResourceIndex) {
      return Failure("geometry references no buffer");
    }
    if (index.get() >= buffers.RowCount()) {
      return Failure("geometry references buffer " + std::to_string(index.get())
        + " of " + std::to_string(buffers.RowCount()));
    }
    try {
      descriptor = buffers.Row<data::pak::core::BufferResourceDesc>(index);
      payload = buffers.Payload(index);
    } catch (const std::out_of_range&) {
      return Failure("buffer payload range exceeds the buffers data");
    } catch (const std::exception& ex) {
      return Failure(
        std::string("buffer resource cannot be read: ") + ex.what());
    }
    return {};
  }

  auto DecodePositions(const data::pak::core::BufferResourceDesc& descriptor,
    const std::span<const std::byte> payload)
    -> std::expected<std::vector<Position>, std::string>
  {
    if (descriptor.element_format != static_cast<uint8_t>(Format::kUnknown)) {
      return Failure(
        "vertex buffer must be structured (element_format=unknown)");
    }
    if (descriptor.element_stride < sizeof(float) * 3U) {
      return Failure("vertex buffer element_stride is too small for xyz");
    }
    const auto stride = static_cast<size_t>(descriptor.element_stride);
    if (payload.size() % stride != 0U) {
      return Failure("vertex buffer size is not a multiple of element_stride");
    }

    const auto count = payload.size() / stride;
    if (count == 0U) {
      return Failure("vertex buffer is empty");
    }
    auto positions = std::vector<Position> {};
    positions.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      auto record = serio::ByteReader { payload.subspan(i * stride, stride) };
      const auto x = record.Read<float>();
      const auto y = record.Read<float>();
      const auto z = record.Read<float>();
      if (!x || !y || !z) {
        return Failure("vertex buffer element_stride is too small for xyz");
      }
      if (!std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*z)) {
        return Failure(
          "vertex " + std::to_string(i) + " has a non-finite position");
      }
      positions.push_back({ *x, *y, *z });
    }
    return positions;
  }

  auto DecodeIndices(const data::pak::core::BufferResourceDesc& descriptor,
    const std::span<const std::byte> payload)
    -> std::expected<std::vector<uint32_t>, std::string>
  {
    auto element_size = size_t { 0 };
    const auto element_format = static_cast<Format>(descriptor.element_format);
    if (element_format == Format::kR32UInt) {
      element_size = sizeof(uint32_t);
    } else if (element_format == Format::kR16UInt) {
      element_size = sizeof(uint16_t);
    } else if (element_format == Format::kUnknown
      && (descriptor.element_stride == sizeof(uint32_t)
        || descriptor.element_stride == sizeof(uint16_t))) {
      element_size = static_cast<size_t>(descriptor.element_stride);
    } else {
      return Failure("index buffer must be uint16 or uint32");
    }
    if (payload.size() % element_size != 0U) {
      return Failure("index buffer size is not a multiple of its element size");
    }

    const auto count = payload.size() / element_size;
    if (count == 0U) {
      return Failure("index buffer is empty");
    }
    auto indices = std::vector<uint32_t> {};
    indices.reserve(count);
    auto reader = serio::ByteReader { payload };
    for (size_t i = 0; i < count; ++i) {
      if (element_size == sizeof(uint32_t)) {
        const auto value = reader.Read<uint32_t>();
        DCHECK_F(value.has_value(), "index buffer size was checked");
        indices.push_back(value.value_or(0U));
      } else {
        const auto value = reader.Read<uint16_t>();
        DCHECK_F(value.has_value(), "index buffer size was checked");
        indices.push_back(static_cast<uint32_t>(value.value_or(0U)));
      }
    }
    return indices;
  }

  [[nodiscard]] auto Difference(const Position& a, const Position& b) noexcept
    -> Position
  {
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
  }

  [[nodiscard]] auto LengthSquared(const Position& v) noexcept -> float
  {
    return (v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]);
  }

  [[nodiscard]] auto Cross(const Position& a, const Position& b) noexcept
    -> Position
  {
    return {
      (a[1] * b[2]) - (a[2] * b[1]),
      (a[2] * b[0]) - (a[0] * b[2]),
      (a[0] * b[1]) - (a[1] * b[0]),
    };
  }

  //! The smallest edge length a kept triangle has: a millionth of the
  //! largest extent of `positions`.
  [[nodiscard]] auto MinimumEdgeLength(
    const std::span<const Position> positions) -> float
  {
    auto low = positions.front();
    auto high = positions.front();
    for (const auto& p : positions) {
      for (size_t axis = 0; axis < p.size(); ++axis) {
        low.at(axis) = std::min(low.at(axis), p.at(axis));
        high.at(axis) = std::max(high.at(axis), p.at(axis));
      }
    }
    const auto extent = Difference(high, low);
    const auto max_extent = std::max({ extent[0], extent[1], extent[2] });
    constexpr auto kRelativeEdge = 1.0e-6F;
    constexpr auto kAbsoluteEdge = 1.0e-8F;
    return std::max(max_extent * kRelativeEdge, kAbsoluteEdge);
  }

  //! Builds the triangles of every mesh view; a view's indices are relative
  //! to its first vertex.
  auto BuildTriangles(const std::span<const uint32_t> indices,
    const std::span<const data::pak::geometry::MeshViewDesc> mesh_views,
    CookedMeshSurface& surface) -> std::expected<void, std::string>
  {
    const auto positions = std::span<const Position>(surface.positions);
    const auto min_edge = MinimumEdgeLength(positions);
    const auto min_edge_sq = min_edge * min_edge;
    const auto min_area2_sq = min_edge_sq * min_edge_sq;
    const auto vertex_count = positions.size();

    auto degenerate = size_t { 0 };
    for (size_t view_index = 0; view_index < mesh_views.size(); ++view_index) {
      const auto& view = mesh_views[view_index];
      const auto view_name = "mesh view " + std::to_string(view_index);
      const auto first_index = static_cast<size_t>(view.first_index);
      const auto index_count = static_cast<size_t>(view.index_count);
      const auto first_vertex = static_cast<size_t>(view.first_vertex);
      const auto view_vertex_count = static_cast<size_t>(view.vertex_count);
      if (index_count % 3U != 0U) {
        return Failure(view_name + " index_count " + std::to_string(index_count)
          + " is not a multiple of 3");
      }
      if (first_index > indices.size()
        || indices.size() - first_index < index_count) {
        return Failure(view_name + " indices exceed the index buffer");
      }
      if (first_vertex > vertex_count
        || vertex_count - first_vertex < view_vertex_count) {
        return Failure(view_name + " vertices exceed the vertex buffer");
      }

      for (size_t i = 0; i < index_count; i += 3U) {
        auto triangle = std::array<uint32_t, 3> {};
        for (size_t corner = 0; corner < triangle.size(); ++corner) {
          const auto local = indices[first_index + i + corner];
          if (local >= view_vertex_count) {
            return Failure(view_name + " index " + std::to_string(local)
              + " is outside its " + std::to_string(view_vertex_count)
              + " vertices");
          }
          triangle.at(corner) = static_cast<uint32_t>(first_vertex + local);
        }
        const auto& p0 = oxygen::base::CheckedAt(positions, triangle[0]);
        const auto& p1 = oxygen::base::CheckedAt(positions, triangle[1]);
        const auto& p2 = oxygen::base::CheckedAt(positions, triangle[2]);
        const auto e01 = Difference(p1, p0);
        const auto e02 = Difference(p2, p0);
        const auto has_short_edge = LengthSquared(e01) <= min_edge_sq
          || LengthSquared(e02) <= min_edge_sq
          || LengthSquared(Difference(p2, p1)) <= min_edge_sq;
        const auto has_no_area = LengthSquared(Cross(e01, e02)) <= min_area2_sq;
        if (has_short_edge || has_no_area) {
          ++degenerate;
          continue;
        }
        surface.triangles.push_back(triangle);
      }
    }

    if (surface.triangles.empty()) {
      return Failure(degenerate > 0U
          ? "every triangle of the geometry is degenerate"
          : "the geometry has no triangles");
    }
    if (degenerate > 0U) {
      DLOG_F(
        1, "Cooked mesh surface dropped {} degenerate triangles", degenerate);
    }
    return {};
  }

  auto ReadStandardSurface(
    const MeshSource& source, const cooked::CookedContent& content)
    -> std::expected<CookedMeshSurface, std::string>
  {
    const auto& buffers = content.ResourceTable(data::ResourceKind::kBuffer);
    auto vertex_descriptor = data::pak::core::BufferResourceDesc {};
    auto index_descriptor = data::pak::core::BufferResourceDesc {};
    auto vertex_payload = std::vector<std::byte> {};
    auto index_payload = std::vector<std::byte> {};
    if (auto read = ReadBuffer(
          buffers, source.vertex_buffer, vertex_descriptor, vertex_payload);
      !read) {
      return std::unexpected(std::move(read.error()));
    }
    if (auto read = ReadBuffer(
          buffers, source.index_buffer, index_descriptor, index_payload);
      !read) {
      return std::unexpected(std::move(read.error()));
    }
    auto positions = DecodePositions(vertex_descriptor, vertex_payload);
    if (!positions) {
      return std::unexpected(std::move(positions.error()));
    }
    const auto indices = DecodeIndices(index_descriptor, index_payload);
    if (!indices) {
      return std::unexpected(indices.error());
    }
    auto surface = CookedMeshSurface { .positions = std::move(*positions) };
    if (auto built = BuildTriangles(*indices, source.mesh_views, surface);
      !built) {
      return std::unexpected(std::move(built.error()));
    }
    return surface;
  }

  auto ReadProceduralSurface(const MeshSource& source)
    -> std::expected<CookedMeshSurface, std::string>
  {
    const auto generated = data::GenerateMeshBuffers(
      source.procedural_name, source.procedural_params);
    if (!generated.has_value()) {
      return Failure(
        "procedural mesh '" + source.procedural_name + "' cannot be generated");
    }
    const auto& [vertices, indices] = *generated;
    if (vertices.empty()) {
      return Failure(
        "procedural mesh '" + source.procedural_name + "' has no vertices");
    }
    auto surface = CookedMeshSurface {};
    surface.positions.reserve(vertices.size());
    for (const auto& vertex : vertices) {
      const auto& p = vertex.position;
      if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
        return Failure("procedural mesh '" + source.procedural_name
          + "' has a non-finite position");
      }
      surface.positions.push_back({ p.x, p.y, p.z });
    }
    if (auto built = BuildTriangles(indices, source.mesh_views, surface);
      !built) {
      return std::unexpected(std::move(built.error()));
    }
    return surface;
  }

} // namespace

auto ReadCookedMeshSurface(
  const cooked::CookedContent& content, const data::AssetKey& geometry_key)
  -> std::expected<CookedMeshSurface, std::string>
{
  const auto* const asset = content.FindAsset(geometry_key);
  if (asset == nullptr) {
    return Failure("the cooked root does not list the geometry");
  }
  auto descriptor_bytes = std::vector<std::byte> {};
  try {
    descriptor_bytes = content.Descriptor(geometry_key);
  } catch (const std::exception& ex) {
    return Failure(
      std::string("geometry descriptor cannot be read: ") + ex.what());
  }
  const auto source = ReadMeshSource(descriptor_bytes, asset->references);
  if (!source) {
    return std::unexpected(source.error());
  }
  switch (source->kind) {
  case MeshSource::Kind::kStandard:
    return ReadStandardSurface(*source, content);
  case MeshSource::Kind::kProcedural:
    return ReadProceduralSurface(*source);
  }
  return Failure("geometry mesh must be standard or procedural");
}

} // namespace oxygen::content::import::internal
