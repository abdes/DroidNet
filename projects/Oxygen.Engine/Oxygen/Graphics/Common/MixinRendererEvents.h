//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Signals.h"

namespace oxygen::graphics {

//! Mixin class to add event slots for the render lifecycle (init, shutdown)
//! and the frame rendering process (begin, end).
/*!
  \note The events are emitted by the renderer implementation and can be
  connected to by the application to perform additional tasks at each stage of
  the rendering process.
 */
// ReSharper disable once CppClassCanBeFinal (mixin should never be made final)
template <typename Base>
class MixinRendererEvents : public Base
{
 public:
  //! Event type definition for the renderer lifecycle events.
  using RendererEvent = sigslot::signal<>;

  //! Event type definition for the frame rendering events.
  using FrameEvent = sigslot::signal<uint32_t>;

  //! Constructor to forward the arguments to the other mixins in the chain.
  //! This method itself does not expect any arguments.
  template <typename... Args>
  constexpr explicit MixinRendererEvents(Args&&... args)
    : Base(std::forward<Args>(args)...)
  {
  }

  virtual ~MixinRendererEvents() = default;

  OXYGEN_DEFAULT_COPYABLE(MixinRendererEvents);
  OXYGEN_DEFAULT_MOVABLE(MixinRendererEvents);

  //! Methods to set up handlers for each event.
  //! @{

  auto OnRendererInitialized() -> RendererEvent& { return renderer_initialized_; }
  auto OnRendererShutdown() -> RendererEvent& { return renderer_shutdown_; }
  auto OnBeginFrameRender() -> FrameEvent& { return begin_frame_render_; }
  auto OnEndFrameRender() -> FrameEvent& { return end_frame_render_; }

  //! @}

 protected:
  //! Helper methods, used by the renderer class to emit events when appropriate.
  //! @{

  void EmitRendererInitialized() { renderer_initialized_(); }
  void EmitRendererShutdown() { renderer_shutdown_(); }
  void EmitBeginFrameRender(uint32_t frame_index) const { begin_frame_render_(frame_index); }
  void EmitEndFrameRender(uint32_t frame_index) const { end_frame_render_(frame_index); }

  //! @}

 private:
  RendererEvent renderer_initialized_; //!< Render has just been initialized.
  RendererEvent renderer_shutdown_; //!< Render is about to be shut down.
  FrameEvent begin_frame_render_; //!< Frame rendering is about to begin.
  FrameEvent end_frame_render_; //!< Frame rendering has just ended.
};

} // namespace oxygen::graphics
