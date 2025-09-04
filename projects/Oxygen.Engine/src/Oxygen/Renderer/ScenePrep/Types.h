//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <utility>

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

namespace oxygen::engine {
class View;
}

namespace oxygen::engine::sceneprep {

//! Bitset representing which render passes an item participates in.
/*!
 Each bit corresponds to a renderer-defined pass ID. Pass assignment is
 deterministic: equivalent inputs must produce identical masks. Use the
 bitwise NamedType helpers to construct, combine and query masks.
*/
using PassMask = oxygen::NamedType<uint32_t,
  // clang-format off
  struct PassMaskTag,
  oxygen::BitWiseAndable,
  oxygen::BitWiseOrable,
  oxygen::BitWiseXorable,
  oxygen::BitWiseLeftShiftable,
  oxygen::BitWiseRightShiftable,
  oxygen::BitWiseInvertable,
  oxygen::Hashable>; // clang-format on

//! Handle to a transform entry managed by ScenePrep.
/*!
 Transform handles reference deduplicated world transforms stored by
 the ScenePrep transform manager. Handles are stable for the lifetime
 of the residency entry but may be recycled over long-running
 execution; do not assume monotonically increasing values. Use
 `TransformHandle::get()` to retrieve the underlying integer index when
 interacting with low-level APIs.
*/
using TransformHandle = oxygen::NamedType<uint32_t,
  // clang-format off
  struct TransformHandleTag,
  oxygen::Hashable,
  oxygen::Comparable>; // clang-format on

//! Handle to a material registered with ScenePrep.
/*!
 Material handles identify materials that have been registered in the
 material registry. They are stable while the material remains in the
 registry. MaterialHandle values should not be interpreted or manipulated
 numerically; use `.get()` only for interop cases (for example when
 filling GPU descriptors).
*/
using MaterialHandle = oxygen::NamedType<uint32_t,
  // clang-format off
  struct MaterialHandleTag,
  oxygen::Hashable,
  oxygen::Comparable>; // clang-format on

//! GPU resource handles for geometry assets.
/*!
 Represents the GPU-resident buffer identifiers for a geometry asset.
 Fields are opaque bindless indices into the renderer's resource tables.
 A value of 0 indicates an invalid or uninitialized handle.
 */
struct GeometryHandle {
  uint32_t vertex_buffer = 0;
  uint32_t index_buffer = 0;
};

//! Draw metadata for GPU command generation.
/*!
 Minimal set of parameters required to emit a draw call. Fields are
 zero-initialized; populate the indexed or non-indexed subset depending on the
 draw type. Optional fields are provided for debugging and validation.
 */
struct DrawMetadata {
  // Indexed draw parameters
  uint32_t first_index = 0;
  uint32_t index_count = 0;
  uint32_t base_vertex = 0;

  // Non-indexed draw parameters
  uint32_t first_vertex = 0;
  uint32_t vertex_count = 0;

  // Optional debugging/validation data
  uint32_t draw_id = 0;
  uint32_t mesh_id = 0;
  uint32_t submesh_id = 0;
};

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
    , view { std::ref(v) }
    , scene { std::ref(s) }
  {
  }

  OXYGEN_DEFAULT_COPYABLE(ScenePrepContext)
  OXYGEN_DEFAULT_MOVABLE(ScenePrepContext)

  ~ScenePrepContext() noexcept = default;

  [[nodiscard]] auto GetFrameId() const noexcept { return frame_id; }
  [[nodiscard]] auto& GetView() const noexcept { return view.get(); }
  [[nodiscard]] auto& GetScene() const noexcept { return scene.get(); }
  // NOTE: RenderContext removed; reintroduce if extractors require GPU ops.

private:
  //! Current frame identifier for temporal coherency optimizations.
  uint64_t frame_id; // TODO: strong type

  //! View containing camera matrices and frustum for the current frame.
  std::reference_wrapper<const View> view;

  //! Scene graph being processed.
  std::reference_wrapper<const scene::Scene> scene;

  // (RenderContext placeholder removed)
};

} // namespace oxygen::engine::sceneprep
