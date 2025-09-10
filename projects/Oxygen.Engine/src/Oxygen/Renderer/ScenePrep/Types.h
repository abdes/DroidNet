//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>

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

constexpr auto to_string(GeometryHandle h)
{
  return "GeoH(" + std::to_string(h.get()) + ")";
}

//! Shared context passed to ScenePrep algorithms.
/*!
 Provides read-only frame, view and scene information along with a mutable
 `RenderContext` for resource operations. The context must outlive the
 ScenePrep invocation that receives it.
 */
class ScenePrepContext {
public:
  //! Construct a ScenePrepContext that borrows the provided references.
  ScenePrepContext(uint64_t fid, const View& v, const scene::Scene& s) noexcept
    : frame_id { fid }
    , view_ { std::ref(v) }
    , scene_ { std::ref(s) }
  {
  }

  OXYGEN_DEFAULT_COPYABLE(ScenePrepContext)
  OXYGEN_DEFAULT_MOVABLE(ScenePrepContext)

  ~ScenePrepContext() noexcept = default;

  [[nodiscard]] auto GetFrameId() const noexcept { return frame_id; }
  [[nodiscard]] auto& GetView() const noexcept { return view_.get(); }
  [[nodiscard]] auto& GetScene() const noexcept { return scene_.get(); }
  // NOTE: RenderContext removed; reintroduce if extractors require GPU ops.

private:
  //! Current frame identifier for temporal coherency optimizations.
  uint64_t frame_id; // TODO: strong type

  //! View containing camera matrices and frustum for the current frame.
  std::reference_wrapper<const View> view_;

  //! Scene graph being processed.
  std::reference_wrapper<const scene::Scene> scene_;

  // (RenderContext placeholder removed)
};

} // namespace oxygen::engine::sceneprep
