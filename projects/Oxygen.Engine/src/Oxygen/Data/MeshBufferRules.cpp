//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include <fmt/format.h>
#include <glm/mat4x4.hpp>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MeshBufferRules.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/ResourceDescriptor.h>
#include <Oxygen/Data/Vertex.h>

namespace oxygen::data {

namespace {

  using detail::IndexBufferView;
  using detail::IndexType;
  using pak::core::BufferResourceDesc;

  //! What a skin stream must hold.
  struct SkinStream final {
    std::string_view name;
    std::uint64_t stride = 0;
    std::uint64_t count = 0;
  };

  //! The elements `row` holds, which must be of `stream.stride` bytes.
  auto ElementsOf(const BufferResourceDesc& row, const SkinStream& stream)
    -> std::uint64_t
  {
    const auto found_stride = BufferElementStride(row);
    if (found_stride != stream.stride) {
      throw CookedFormatError(CookedDefect::kWrongRowSize,
        fmt::format("The {} stream has elements of {} bytes, not of {}",
          stream.name, found_stride, stream.stride));
    }
    return row.size_bytes / stream.stride;
  }

  //! Checks that `row` holds an element for each of `stream.count` vertices.
  auto CheckPerVertexStream(
    const BufferResourceDesc& row, const SkinStream& stream) -> void
  {
    const auto found = ElementsOf(row, stream);
    if (found != stream.count) {
      throw CookedFormatError(CookedDefect::kSizeMismatch,
        fmt::format("The {} stream holds {} elements for {} vertices",
          stream.name, found, stream.count));
    }
  }

  //! Checks that `row` holds an element for each of `stream.count` joints.
  auto CheckPerJointStream(
    const BufferResourceDesc& row, const SkinStream& stream) -> void
  {
    const auto found = ElementsOf(row, stream);
    if (found < stream.count) {
      throw CookedFormatError(CookedDefect::kSizeMismatch,
        fmt::format("The {} stream holds {} elements for {} joints",
          stream.name, found, stream.count));
    }
  }

} // namespace

auto CheckVertexBufferRow(const BufferResourceDesc& row) -> void
{
  const auto stride = BufferElementStride(row);
  if (stride != sizeof(Vertex)) {
    throw CookedFormatError(CookedDefect::kWrongRowSize,
      fmt::format("The vertex buffer has elements of {} bytes, not of {}",
        stride, sizeof(Vertex)));
  }
  if (row.size_bytes == 0U || row.size_bytes % sizeof(Vertex) != 0U) {
    throw CookedFormatError(CookedDefect::kWrongRowSize,
      fmt::format(
        "The vertex buffer of {} bytes does not hold whole vertices of {}",
        row.size_bytes, sizeof(Vertex)));
  }
}

auto IndexTypeOf(const Format format, const std::uint32_t stride) -> IndexType
{
  const auto unformatted = format == Format::kUnknown;
  const auto narrow
    = format == Format::kR16UInt || (unformatted && stride == 2U);
  const auto wide = format == Format::kR32UInt || (unformatted && stride == 4U);
  if (!narrow && !wide) {
    throw CookedFormatError(CookedDefect::kInvalidValue,
      "The index buffer does not hold 16-bit or 32-bit indices");
  }
  return narrow ? IndexType::kUInt16 : IndexType::kUInt32;
}

auto CheckMeshViewRange(const MeshViewRange& view,
  const std::size_t vertex_count, const IndexBufferView& indices) -> void
{
  // Subtracting from the buffer sizes, which a range cannot exceed, does not
  // wrap as adding a range's start and count can.
  const auto holds_vertices = view.first_vertex <= vertex_count
    && view.vertex_count <= vertex_count - view.first_vertex;
  const auto holds_indices = view.first_index <= indices.Count()
    && view.index_count <= indices.Count() - view.first_index;
  if (!holds_vertices || !holds_indices) {
    throw CookedFormatError(CookedDefect::kOutOfBounds,
      fmt::format("The mesh view of {} vertices from {} and {} indices from {} "
                  "is past its buffers of {} vertices and {} indices",
        view.vertex_count, view.first_vertex, view.index_count,
        view.first_index, vertex_count, indices.Count()));
  }
  const auto drawn = indices.SliceElements(view.first_index, view.index_count);
  const auto in_view = std::ranges::all_of(
    drawn.Widened(), [&view](const std::uint32_t index) -> bool {
      return index < view.vertex_count;
    });
  if (!in_view) {
    throw CookedFormatError(CookedDefect::kOutOfBounds,
      fmt::format("An index of the mesh view is past its {} vertices",
        view.vertex_count));
  }
}

auto CheckSkinBufferRows(const SkinBufferRows& rows) -> void
{
  const auto influence_bytes
    = std::uint64_t { rows.influences_per_vertex } * sizeof(std::uint32_t);
  CheckPerVertexStream(rows.joint_indices,
    {
      .name = "joint index",
      .stride = influence_bytes,
      .count = rows.vertex_count,
    });
  CheckPerVertexStream(rows.joint_weights,
    {
      .name = "joint weight",
      .stride = influence_bytes,
      .count = rows.vertex_count,
    });
  CheckPerJointStream(rows.inverse_bind_matrices,
    {
      .name = "inverse bind matrix",
      .stride = sizeof(glm::mat4),
      .count = rows.joint_count,
    });
  CheckPerJointStream(rows.joint_remap,
    {
      .name = "joint remap",
      .stride = sizeof(std::uint32_t),
      .count = rows.joint_count,
    });
}

auto CheckJointIndices(const std::span<const std::byte> joint_indices,
  const std::uint32_t joint_count) -> void
{
  constexpr auto kValueSize = sizeof(std::uint32_t);
  if (joint_indices.size() % kValueSize != 0U) {
    throw CookedFormatError(CookedDefect::kSizeMismatch,
      fmt::format("The joint index stream of {} bytes holds part of a value",
        joint_indices.size()));
  }
  for (std::size_t offset = 0; offset < joint_indices.size();
    offset += kValueSize) {
    auto joint = std::uint32_t { 0 };
    std::memcpy(
      &joint, joint_indices.subspan(offset, kValueSize).data(), kValueSize);
    if (joint >= joint_count) {
      throw CookedFormatError(CookedDefect::kOutOfBounds,
        fmt::format("The joint index {} is past the mesh's {} joints", joint,
          joint_count));
    }
  }
}

} // namespace oxygen::data
