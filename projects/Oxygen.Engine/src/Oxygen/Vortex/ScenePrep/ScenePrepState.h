//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cassert>
#include <ranges>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Vortex/Resources/DrawMetadataEmitter.h>
#include <Oxygen/Vortex/Resources/GeometryUploader.h>
#include <Oxygen/Vortex/Resources/MaterialBinder.h>
#include <Oxygen/Vortex/Resources/TransformUploader.h>
#include <Oxygen/Vortex/ScenePrep/RenderItemData.h>
#include <Oxygen/Vortex/Types/PassMask.h>

namespace oxygen::data {
class GeometryAsset;
} // namespace oxygen::data

namespace oxygen::vortex::sceneprep {

//! Persistent and per-frame state for ScenePrep operations. Manages both
//! temporary data (cleared each frame) and persistent caches (reused across
//! frames).
class ScenePrepState {
public:
  struct ViewLocalData {
    std::vector<RenderItemData> collected_items;
    std::vector<std::size_t> retained_indices;
    std::vector<PassMask> pass_masks;
  };

  struct CachedNodeBasics {
    scene::NodeHandle node_handle;
    bool cast_shadows { true };
    bool receive_shadows { true };
    glm::mat4 world_transform { 1.0F };
    std::shared_ptr<const oxygen::data::GeometryAsset> geometry;
    TransformHandle transform_handle { kInvalidTransformHandle };
  };

  ScenePrepState() = default;

  /*!
   Construct ScenePrepState taking ownership of optional persistent resource
   managers. Any argument may be null. Users may observe them via the provided
   getter methods which return oxygen::observer_ptr to express non-ownership.
  */
  ScenePrepState(std::unique_ptr<resources::GeometryUploader> geometry,
    std::unique_ptr<resources::TransformUploader> transform,
    std::unique_ptr<resources::MaterialBinder> material,
    std::unique_ptr<resources::DrawMetadataEmitter> draw_emitter
    = nullptr) noexcept
    : geometry_uploader_(std::move(geometry))
    , transform_mgr_(std::move(transform))
    , material_binder_(std::move(material))
    , draw_emitter_(std::move(draw_emitter))
  {
  }

  OXYGEN_MAKE_NON_COPYABLE(ScenePrepState)
  OXYGEN_MAKE_NON_MOVABLE(ScenePrepState)

  ~ScenePrepState()
  {
    // Ordered destruction of members
    draw_emitter_.reset();
    material_binder_.reset();
    transform_mgr_.reset();
    geometry_uploader_.reset();
  }

  auto ReserveCapacityForItems(const std::size_t item_count) -> void
  {
    view_local_.collected_items.reserve(
      view_local_.collected_items.size() + item_count);
    view_local_.retained_indices.reserve(
      view_local_.retained_indices.size() + item_count);
  }

  auto CollectedCount() const -> std::size_t
  {
    return view_local_.collected_items.size();
  }

  auto CollectedItems() const -> std::span<const RenderItemData>
  {
    return { view_local_.collected_items.data(),
      view_local_.collected_items.size() };
  }

  auto CollectItem(RenderItemData item) -> void
  {
    view_local_.collected_items.push_back(std::move(item));
  }

  auto MarkItemRetained(const std::size_t index) -> void
  {
    assert(not std::ranges::contains(view_local_.retained_indices, index));
    assert(index < view_local_.collected_items.size());
    view_local_.retained_indices.push_back(index);
  }

  auto RetainedCount() const -> std::size_t
  {
    return view_local_.retained_indices.size();
  }

  auto RetainedItems() const
  {
    return view_local_.retained_indices
      | std::views::transform(
        [&](const std::size_t i) -> const RenderItemData& {
          return view_local_.collected_items[i];
        });
  }

  auto RetainedItems()
  {
    return view_local_.retained_indices
      | std::views::transform([&](const std::size_t i) -> RenderItemData& {
          return view_local_.collected_items[i];
        });
  }

  [[nodiscard]] auto PassMasks() noexcept -> std::vector<PassMask>&
  {
    return view_local_.pass_masks;
  }

  [[nodiscard]] auto PassMasks() const noexcept -> const std::vector<PassMask>&
  {
    return view_local_.pass_masks;
  }

  //! Get non-owning observer to geometry uploader (maybe nullptr).
  constexpr auto GetGeometryUploader() const noexcept
    -> observer_ptr<resources::GeometryUploader>
  {
    return observer_ptr(geometry_uploader_.get());
  }

  //! Get non-owning observer to transform manager (maybe nullptr).
  constexpr auto GetTransformUploader() const noexcept
    -> observer_ptr<resources::TransformUploader>
  {
    return observer_ptr(transform_mgr_.get());
  }

  //! Get non-owning observer to material binder (maybe nullptr).
  constexpr auto GetMaterialBinder() const noexcept
    -> observer_ptr<resources::MaterialBinder>
  {
    return observer_ptr(material_binder_.get());
  }

  //! Get non-owning observer to draw metadata emitter (maybe nullptr).
  constexpr auto GetDrawMetadataEmitter() const noexcept
    -> observer_ptr<resources::DrawMetadataEmitter>
  {
    return observer_ptr(draw_emitter_.get());
  }

  //! Reset per-frame data while preserving persistent caches.
  auto ResetFrameData() -> void
  {
    view_local_ = {};
    if (draw_emitter_) {
      draw_emitter_->ResetViewData();
    }
    // Clear cached per-frame filtered node list populated during Frame-phase
    filtered_scene_nodes_.clear();
    cached_node_basics_.clear();
  }

  //! Reset per-view data while keeping global lists/caches intact.
  auto ResetViewData() -> void
  {
    // Clear per-view transient data while preserving frame-phase caches
    // (filtered node list and cached node basics) so later views can reuse the
    // frame traversal without inheriting the previous view's collected items.
    view_local_ = {};
    if (draw_emitter_) {
      draw_emitter_->ResetViewData();
    }
  }

  //! Add a node pointer to the cached filtered scene nodes list. Populated
  //! during Frame-phase traversal and used during View-phase to reconstruct
  //! `RenderItemProto` objects rapidly without scanning the full scene.
  //! This method deduplicates consecutive inserts (many producers emit
  //! multiple RenderItemData entries per node) by checking the last added
  //! node pointer.
  auto AddFilteredSceneNode(const scene::SceneNodeImpl* node) -> void
  {
    if (node == nullptr) {
      return;
    }
    if (filtered_scene_nodes_.empty() || filtered_scene_nodes_.back() != node) {
      filtered_scene_nodes_.push_back(node);
    }
  }

  //! Accessor for the cached filtered scene nodes.
  auto GetFilteredSceneNodes() const
    -> const std::vector<const scene::SceneNodeImpl*>&
  {
    return filtered_scene_nodes_;
  }

  auto CacheNodeBasics(
    const scene::SceneNodeImpl* node, const CachedNodeBasics& basics) -> void
  {
    if (node == nullptr) {
      return;
    }
    cached_node_basics_[node] = basics;
  }

  auto TryGetNodeBasics(const scene::SceneNodeImpl* node) const
    -> const CachedNodeBasics*
  {
    if (node == nullptr) {
      return nullptr;
    }
    const auto it = cached_node_basics_.find(node);
    return it != cached_node_basics_.end() ? &it->second : nullptr;
  }

private:
  ViewLocalData view_local_ {};

  //! Modern geometry uploader with stable handles and bindless access.
  std::unique_ptr<resources::GeometryUploader> geometry_uploader_;

  //! Persistent transform deduplication and GPU buffer management.
  std::unique_ptr<resources::TransformUploader> transform_mgr_;

  //! Persistent material deduplication and GPU buffer management.
  std::unique_ptr<resources::MaterialBinder> material_binder_;

  //! Dynamic draw metadata builder and uploader (no atlas; fully dynamic)
  std::unique_ptr<resources::DrawMetadataEmitter> draw_emitter_;

  //! Cached ordered list of scene nodes that were processed during the
  //! Frame-phase and passed the pre-filter. The pointers are non-owning and
  //! valid for the duration of a frame; they must not be persisted across
  //! frames. This list is deduplicated for consecutive entries to avoid
  //! repeating the same node when a producer emits multiple per-node items.
  std::vector<const scene::SceneNodeImpl*> filtered_scene_nodes_;

  //! Cached view-invariant extraction outputs for nodes processed in
  //! frame-phase. Reused in view-phase to avoid redundant pre-filter work.
  std::unordered_map<const scene::SceneNodeImpl*, CachedNodeBasics>
    cached_node_basics_;
  // (no upload ticket storage here - upload coordination is external)
};

} // namespace oxygen::vortex::sceneprep
