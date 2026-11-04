//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! The vertices and indices one mesh view draws.
struct MeshViewRange final {
  std::uint32_t first_vertex = 0;
  std::uint32_t vertex_count = 0;
  std::uint32_t first_index = 0;
  std::uint32_t index_count = 0;
};

//! The buffer rows of a skinned mesh's streams, with what they must serve.
struct SkinBufferRows final {
  pak::core::BufferResourceDesc joint_indices {};
  pak::core::BufferResourceDesc joint_weights {};
  pak::core::BufferResourceDesc inverse_bind_matrices {};
  pak::core::BufferResourceDesc joint_remap {};
  std::uint64_t vertex_count = 0;
  std::uint32_t joint_count = 0;
  std::uint32_t influences_per_vertex = 0;
};

//! Checks that a buffer row describes whole `Vertex` records: its element is
//! one vertex, so its views count vertices.
/*!
 @throw CookedFormatError When its element is another size, or it holds part
   of a vertex or none.
*/
OXGN_DATA_API auto CheckVertexBufferRow(
  const pak::core::BufferResourceDesc& row) -> void;

//! Checks that the position, normal and texture coordinates of every vertex
//! are finite: the renderer refuses a mesh with any that is not.
/*!
 @param vertices The buffer's bytes: whole `Vertex` records, at any address.
 @throw CookedFormatError When a value is NaN or infinite.
*/
OXGN_DATA_API auto CheckVertexValues(std::span<const std::byte> vertices)
  -> void;

//! The type of the indices a buffer holds, which its format names, or its
//! stride for a buffer without one.
/*!
 @throw CookedFormatError When it holds neither 16-bit nor 32-bit indices.
*/
OXGN_DATA_NDAPI auto IndexTypeOf(Format format, std::uint32_t stride)
  -> detail::IndexType;

//! Checks that buffers of `vertex_count` vertices and of `indices` hold the
//! ranges of `view`, and that each of its indices, which count from its first
//! vertex, names one of its vertices.
/*!
 @throw CookedFormatError When a range leaves its buffer or an index its view.
*/
OXGN_DATA_API auto CheckMeshViewRange(const MeshViewRange& view,
  std::size_t vertex_count, const detail::IndexBufferView& indices) -> void;

//! Checks that a skinned mesh's streams hold what it names: joint indices and
//! weights of `influences_per_vertex` 32-bit values for each vertex, and an
//! inverse bind matrix and a remap entry for each joint.
/*!
 @throw CookedFormatError When a stream has another element or too few of them.
*/
OXGN_DATA_API auto CheckSkinBufferRows(const SkinBufferRows& rows) -> void;

//! Checks that every joint index in a joint index stream names one of the
//! mesh's `joint_count` joints.
/*!
 @param joint_indices The stream's bytes: 32-bit values, at any address.
 @throw CookedFormatError When an index is past the last joint, or the stream
   holds part of a value.
*/
OXGN_DATA_API auto CheckJointIndices(
  std::span<const std::byte> joint_indices, std::uint32_t joint_count) -> void;

} // namespace oxygen::data
