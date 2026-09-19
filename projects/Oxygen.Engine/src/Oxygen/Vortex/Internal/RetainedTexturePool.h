//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen {
class Graphics;
namespace graphics {
  class Texture;
  struct TextureDesc;
} // namespace graphics
} // namespace oxygen

namespace oxygen::vortex::internal {

//! Retains one reusable, fence-retired output texture per active view.
/*!
 Each instance owns one output kind. Returned wrappers keep the texture and its
 descriptors alive until the last reader releases them and its frame retires.
 Pool operations run on the rendering thread; wrappers may outlive the pool.
*/
class RetainedTexturePool final {
public:
  OXGN_VRTX_API explicit RetainedTexturePool(
    std::shared_ptr<Graphics> graphics);
  OXGN_VRTX_API ~RetainedTexturePool();

  OXYGEN_MAKE_NON_COPYABLE(RetainedTexturePool)
  OXYGEN_MAKE_NON_MOVABLE(RetainedTexturePool)

  //! Stateless outputs receive retained ownership without entering the cache.
  [[nodiscard]] OXGN_VRTX_API auto Acquire(
    ViewId view_id, const graphics::TextureDesc& desc, bool recyclable)
    -> std::shared_ptr<graphics::Texture>;
  OXGN_VRTX_API auto OnFrameStart(frame::SequenceNumber sequence) -> void;
  OXGN_VRTX_API auto RemoveView(ViewId view_id) -> void;
  OXGN_VRTX_API auto Clear() -> void;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::vortex::internal
