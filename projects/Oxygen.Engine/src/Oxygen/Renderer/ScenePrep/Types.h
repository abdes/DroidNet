//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/Types/Frame.h>

namespace oxygen::data {
class GeometryAsset;
class MaterialAsset;
class Mesh;
class MeshView;
} // namespace oxygen::data

namespace oxygen::engine::extraction {
struct RenderItemData;
} // namespace oxygen::engine::extraction

namespace oxygen::scene {
class Scene;
} // namespace oxygen::scene

namespace oxygen {
class View;
}

namespace oxygen::engine::sceneprep {

//! Handle to a transform entry managed by ScenePrep.
/*!
 Transform handles reference deduplicated world transforms stored by
 the ScenePrep transform manager. Handles are stable for the lifetime
 of the residency entry but may be recycled over long-running
 execution; do not assume monotonically increasing values. Use
 `TransformHandle::get()` to retrieve the underlying integer index when
 interacting with low-level APIs.
*/
using TransformHandle
  = NamedType<uint32_t, // TODO: replace with VersionedHandle?
                        // clang-format off
  struct TransformHandleTag,
  Arithmetic>; // clang-format on

//! Invalid TransformHandle sentinel value.
//! TODO: Replace with proper invalid handle constant once VersionedHandle is
//! implemented
inline constexpr TransformHandle kInvalidTransformHandle { (
  std::numeric_limits<std::uint32_t>::max)() };

constexpr auto to_string(TransformHandle h)
{
  return "TransH(" + std::to_string(h.get()) + ")";
}

//! Handle to a material registered with ScenePrep.
/*!
 Material handles identify materials that have been registered in the
 material registry. They are stable while the material remains in the
 registry. MaterialHandle values should not be interpreted or manipulated
 numerically; use `.get()` only for interop cases (for example when
 filling GPU descriptors).
*/
using MaterialHandle
  = NamedType<uint32_t, // TODO: replace with VersionedHandle?
                        // clang-format off
  struct MaterialHandleTag,
  Arithmetic>; // clang-format on

//! Invalid MaterialHandle sentinel value.
//! TODO: Replace with proper invalid handle constant once VersionedHandle is
//! implemented
inline constexpr MaterialHandle kInvalidMaterialHandle { (
  std::numeric_limits<std::uint32_t>::max)() };

constexpr auto to_string(MaterialHandle h)
{
  return "MatH(" + std::to_string(h.get()) + ")";
}

//! Handle to a geometry entry managed by GeometryUploader.
/*!
 Geometry handles reference deduplicated mesh resources stored by
 the GeometryUploader. Handles are stable for the lifetime of the
 residency entry but may be recycled over long-running execution;
 do not assume monotonically increasing values. Use
 `GeometryHandle::get()` to retrieve the underlying integer index when
 interacting with low-level APIs.
*/
using GeometryHandle
  = NamedType<uint32_t, // TODO: replace with VersionedHandle?
                        // clang-format off
  struct GeometryHandleTag,
  Arithmetic>; // clang-format on

//! Invalid GeometryHandle sentinel value.
//! TODO: Replace with proper invalid handle constant once VersionedHandle is
//! implemented
inline constexpr GeometryHandle kInvalidGeometryHandle { (
  std::numeric_limits<std::uint32_t>::max)() };

constexpr auto to_string(GeometryHandle h)
{
  return "GeoH(" + std::to_string(h.get()) + ")";
}

} // namespace oxygen::engine::sceneprep
