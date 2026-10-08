//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include <glm/vec3.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::vortex {

//! A rectangle of a view's rendered image, in pixels from its top-left corner.
struct ViewPickRect {
  std::uint32_t x { 0U };
  std::uint32_t y { 0U };
  std::uint32_t width { 1U };
  std::uint32_t height { 1U };
};

//! One scene node with visible pixels inside a pick rectangle.
struct ViewPickHit {
  scene::NodeHandle node {};
  //! Device depth of the node's nearest pixel in the rectangle.
  float depth { 0.0F };
  //! Geometry slot (submesh) of that pixel.
  std::uint32_t submesh_index { 0U };
  //! Distance, in pixels, from the rectangle centre to the node's closest
  //! pixel.
  float center_distance { 0.0F };
};

//! The outcome of a pick request.
struct ViewPickResult {
  enum class Status : std::uint8_t {
    //! The pick ran; `hits` may be empty.
    kCompleted,
    //! The view was not rendered, was removed, or the request was replaced.
    kCancelled,
    //! The pick could not be recorded or read back.
    kFailed,
  };

  Status status { Status::kCancelled };
  //! One entry per node, closest to the rectangle centre first, then nearest
  //! in depth.
  std::vector<ViewPickHit> hits;
  //! World position of the first hit's pixel.
  std::optional<glm::vec3> world_position;
};

//! An on-demand pick of one view: which scene nodes have visible geometry
//! inside a rectangle of its rendered image.
/*!
 The renderer draws the view's pickable draws into a target the size of the
 rectangle the next time the view renders, and reads it back asynchronously.
 The completion runs exactly once, on the render thread: with the result, or
 cancelled when the request is dropped before it completes. An idle view pays
 nothing.
*/
class ViewPickRequest {
public:
  using Completion = std::function<void(ViewPickResult)>;

  ViewPickRequest(const ViewPickRect rect, Completion completion)
    : rect_(rect)
    , completion_(std::move(completion))
  {
  }

  ~ViewPickRequest() { Complete(ViewPickResult {}); }

  OXYGEN_MAKE_NON_COPYABLE(ViewPickRequest)
  OXYGEN_MAKE_NON_MOVABLE(ViewPickRequest)

  [[nodiscard]] auto Rect() const noexcept -> const ViewPickRect&
  {
    return rect_;
  }

  //! Delivers the result; later calls do nothing.
  auto Complete(ViewPickResult result) noexcept -> void
  {
    if (auto completion = std::exchange(completion_, {})) {
      try {
        completion(std::move(result));
      } catch (...) { // NOLINT(bugprone-empty-catch)
        // A consumer failure must not escape into the render loop.
      }
    }
  }

private:
  ViewPickRect rect_;
  Completion completion_;
};

} // namespace oxygen::vortex
