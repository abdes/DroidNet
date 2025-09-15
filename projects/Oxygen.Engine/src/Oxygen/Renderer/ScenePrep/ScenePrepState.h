//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cassert>
#include <ranges>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Renderer/Resources/DrawMetadataEmitter.h>
#include <Oxygen/Renderer/Resources/GeometryUploader.h>
#include <Oxygen/Renderer/Resources/MaterialBinder.h>
#include <Oxygen/Renderer/Resources/TransformUploader.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemData.h>
#include <Oxygen/Renderer/Types/PassMask.h>

namespace oxygen::engine::sceneprep {

// Forward: extraction::RenderItemData already declared in Types.h

//! Persistent and per-frame state for ScenePrep operations. Manages both
//! temporary data (cleared each frame) and persistent caches (reused across
//! frames).
class ScenePrepState {
public:
  ScenePrepState() = default;

  /*!
   Construct ScenePrepState taking ownership of optional persistent resource
   managers. Any argument may be null. Users may observe them via the provided
   getter methods which return oxygen::observer_ptr to express non-ownership.
  */
  ScenePrepState(
    std::unique_ptr<renderer::resources::GeometryUploader> geometry,
    std::unique_ptr<renderer::resources::TransformUploader> transform,
    std::unique_ptr<renderer::resources::MaterialBinder> material,
    std::unique_ptr<renderer::resources::DrawMetadataEmitter> draw_emitter
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
    collected_items_.reserve(collected_items_.size() + item_count);
    retained_indices_.reserve(retained_indices_.size() + item_count);
  }

  auto CollectedCount() const -> std::size_t { return collected_items_.size(); }

  auto CollectedItems() const -> std::span<const RenderItemData>
  {
    return { collected_items_.data(), collected_items_.size() };
  }

  auto CollectItem(RenderItemData item) -> void
  {
    collected_items_.push_back(std::move(item));
  }

  auto MarkItemRetained(const std::size_t index) -> void
  {
    assert(not std::ranges::contains(retained_indices_, index));
    assert(index < collected_items_.size());
    retained_indices_.push_back(index);
  }

  auto RetainedCount() const -> std::size_t { return retained_indices_.size(); }

  auto RetainedItems() const
  {
    return retained_indices_
      | std::views::transform(
        [&](const std::size_t i) -> const RenderItemData& {
          return collected_items_[i];
        });
  }

  auto RetainedItems()
  {
    return retained_indices_
      | std::views::transform([&](const std::size_t i) -> RenderItemData& {
          return collected_items_[i];
        });
  }

  //! Pass masks aligned with retained_indices.
  std::vector<PassMask> pass_masks; // TODO: clean-up this public member

  //! Get non-owning observer to geometry uploader (maybe nullptr).
  constexpr auto GetGeometryUploader() const noexcept
    -> observer_ptr<renderer::resources::GeometryUploader>
  {
    return observer_ptr(geometry_uploader_.get());
  }

  //! Get non-owning observer to transform manager (maybe nullptr).
  constexpr auto GetTransformUploader() const noexcept
    -> observer_ptr<renderer::resources::TransformUploader>
  {
    return observer_ptr(transform_mgr_.get());
  }

  //! Get non-owning observer to material binder (maybe nullptr).
  constexpr auto GetMaterialBinder() const noexcept
    -> observer_ptr<renderer::resources::MaterialBinder>
  {
    return observer_ptr(material_binder_.get());
  }

  //! Get non-owning observer to draw metadata emitter (maybe nullptr).
  constexpr auto GetDrawMetadataEmitter() const noexcept
    -> observer_ptr<renderer::resources::DrawMetadataEmitter>
  {
    return observer_ptr(draw_emitter_.get());
  }

  //! Reset per-frame data while preserving persistent caches.
  auto ResetFrameData() -> void
  {
    // Clear collection phase data
    collected_items_.clear();
    retained_indices_.clear();
    pass_masks.clear();
  }

private:
  //! Raw items collected during scene traversal.
  std::vector<RenderItemData> collected_items_;

  //! Indices of items that passed filtering.
  std::vector<std::size_t> retained_indices_;

  //! Modern geometry uploader with deduplication and bindless access.
  std::unique_ptr<renderer::resources::GeometryUploader> geometry_uploader_;

  //! Persistent transform deduplication and GPU buffer management.
  std::unique_ptr<renderer::resources::TransformUploader> transform_mgr_;

  //! Persistent material deduplication and GPU buffer management.
  std::unique_ptr<renderer::resources::MaterialBinder> material_binder_;

  //! Dynamic draw metadata builder and uploader (no atlas; fully dynamic)
  std::unique_ptr<renderer::resources::DrawMetadataEmitter> draw_emitter_;
};

} // namespace oxygen::engine::sceneprep
