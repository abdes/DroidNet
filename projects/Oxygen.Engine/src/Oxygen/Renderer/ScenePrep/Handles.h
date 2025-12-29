//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <limits>
#include <string>

#include <Oxygen/Base/NamedType.h>

// TODO: replace with VersionedHandle?

namespace oxygen::engine::sceneprep {

//! Handle to a transform entry managed by ScenePrep.
/*!
 Assigned during collection via `TransformUploader::GetOrAllocate(matrix)`.

 Provides stable identity for transforms before GPU buffers are allocated as
 part of finalization, and can be used as index into shared GPU transform buffer
 during rendering (bindless access).

 Handles are stable for the lifetime of the residency entry but may be recycled
 over long-running execution; do not assume monotonically increasing values. Use
 `TransformHandle::get()` to retrieve the underlying integer index when
 interacting with low-level APIs.
*/
using TransformHandle = NamedType<uint32_t,
  // clang-format off
  struct TransformHandleTag,
  Arithmetic
  >; // clang-format on

//! Invalid TransformHandle sentinel value.
inline constexpr TransformHandle kInvalidTransformHandle { (
  std::numeric_limits<std::uint32_t>::max)() };

constexpr auto to_string(TransformHandle h)
{
  return "TransH(" + std::to_string(h.get()) + ")";
}

//! Handle to a material registered with ScenePrep.
/*!
 Assigned during collection via `MaterialBinder::GetOrAllocate(material)`.

 Uses content-based hashing: identical materials receive the same handle, while
 unique materials allocate new handles. Multiple items referencing the same
 material receive the same handle at collection time. During finalization,
 handles map to GPU atlas slots and constant buffer entries, enabling bindless
 access during rendering.

 Handles remain stable while materials persist in the registry but may be
 recycled during long-running execution; do not assume monotonically increasing
 values. Use `.get()` only for GPU descriptor interop.
*/
using MaterialHandle = NamedType<uint32_t,
  // clang-format off
  struct MaterialHandleTag,
  Arithmetic
  >; // clang-format on

//! Invalid MaterialHandle sentinel value.
inline constexpr MaterialHandle kInvalidMaterialHandle { (
  std::numeric_limits<std::uint32_t>::max)() };

constexpr auto to_string(MaterialHandle h)
{
  return "MatH(" + std::to_string(h.get()) + ")";
}

//! Handle to a geometry entry managed by GeometryUploader.
/*!
 Assigned during collection via `GeometryUploader::GetOrAllocate(mesh)`.

 Geometry deduplication (identical content resolving to a single asset identity)
 is owned by the asset loader and its cache.

 GeometryUploader may perform lightweight interning: repeated requests for the
 same geometry identity (AssetKey, LOD index) return the same handle.
 GeometryUploader must not attempt runtime content hashing of vertex/index
 buffers.

 During finalization, handles map to GPU vertex/index buffer SRV indices,
 enabling bindless access during rendering.

 Handles remain stable for the lifetime of the residency entry but may be
 recycled during long-running execution; do not assume monotonically increasing
 values. Use `GeometryHandle::get()` to retrieve the underlying integer index
 for low-level APIs.
*/
using GeometryHandle = NamedType<uint32_t,
  // clang-format off
  struct GeometryHandleTag,
  Arithmetic
  >; // clang-format on

//! Invalid GeometryHandle sentinel value.
inline constexpr GeometryHandle kInvalidGeometryHandle { (
  std::numeric_limits<std::uint32_t>::max)() };

constexpr auto to_string(GeometryHandle h)
{
  return "GeoH(" + std::to_string(h.get()) + ")";
}

} // namespace oxygen::engine::sceneprep
