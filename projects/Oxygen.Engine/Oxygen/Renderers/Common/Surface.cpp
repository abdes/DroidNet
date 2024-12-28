//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Common/Surface.h"

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Base/Signals.h"
#include "Oxygen/Platform/Window.h"

using namespace oxygen::renderer;
using namespace oxygen::renderer::resources;

auto WindowSurface::Width() const -> uint32_t
{
  if (const auto window = window_.lock())
    return window->GetFrameBufferSize().width;
  throw std::runtime_error("Window is no longer valid");
}

auto WindowSurface::Height() const -> uint32_t
{
  if (const auto window = window_.lock())
    return window->GetFrameBufferSize().height;
  throw std::runtime_error("Window is no longer valid");
}

void WindowSurface::InitializeSurface()
{
  const auto window = window_.lock();
  if (!window) throw std::runtime_error("Window is no longer valid");

  LOG_F(INFO, "Initializing Window Surface `{}` [{}]", window->Title(), GetId().ToString());

  on_resize_ = std::make_unique<sigslot::connection>(window->OnResized().connect(
    [this](const auto& size)
    {
      LOG_F(1, "Window Surface OnResized() [{}] ", GetId().ToString());
      Resize(size.width, size.height);
    }));
  on_minimized_ = std::make_unique<sigslot::connection>(window->OnMinimized().connect(
    [this]
    {
      LOG_F(1, "Window Surface OnMinimized() [{}]", GetId().ToString());
      // TODO: Window minimized
    }));
  on_restored_ = std::make_unique<sigslot::connection>(window->OnRestored().connect(
    [this]
    {
      LOG_F(1, "Window Surface OnRestored() [{}]", GetId().ToString());
      // TODO: Window restored
    }));
  on_close_ = std::make_unique<sigslot::connection>(window->OnClosing().connect(
    [this]
    {
      LOG_F(INFO, "Window Surface OnClosing() [{}]", GetId().ToString());

      // Disconnect signals
      DCHECK_NOTNULL_F(on_close_);
      on_close_->disconnect();
      DCHECK_NOTNULL_F(on_minimized_);
      on_minimized_->disconnect();
      DCHECK_NOTNULL_F(on_resize_);
      on_resize_->disconnect();
      DCHECK_NOTNULL_F(on_restored_);
      on_restored_->disconnect();

      this->self().Release();
    }));
}

void WindowSurface::ReleaseSurface() noexcept
{
  DCHECK_F(IsValid());

  LOG_F(INFO, "Releasing Window Surface [{}]", GetId().ToString());
}
