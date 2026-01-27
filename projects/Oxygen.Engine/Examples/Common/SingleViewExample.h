//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <string_view>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Scene/SceneNode.h>

#include "Common/AsyncEngineApp.h"
#include "Common/ExampleModuleBase.h"
#include "Common/RenderGraph.h"

namespace oxygen::examples::common {

//! Base class for single-view examples sharing the same renderer wiring.
class SingleViewExample : public ExampleModuleBase {
  OXYGEN_TYPED(SingleViewExample)

public:
  explicit SingleViewExample(const AsyncEngineApp& app);
  ~SingleViewExample() override = default;

  void OnShutdown() noexcept override;

protected:
  using ViewReadyCallback = std::function<void(int width, int height)>;

  auto UpdateFrameContext(
    engine::FrameContext& context, ViewReadyCallback on_view_ready) -> void;

  auto RegisterViewForRendering(scene::SceneNode camera_node) -> void;

  auto UnregisterViewForRendering(std::string_view reason) -> void;

  auto ResolveRenderer() const -> oxygen::engine::Renderer*;

  auto GetRenderGraph() const
    -> oxygen::observer_ptr<oxygen::examples::common::RenderGraph>
  {
    return render_graph_;
  }

  auto GetViewId() const -> ViewId { return view_id_; }

  auto ClearBackbufferReferences() -> void override;

private:
  ViewId view_id_ { kInvalidViewId };
  bool renderer_view_registered_ { false };
  oxygen::observer_ptr<oxygen::examples::common::RenderGraph> render_graph_ {
    nullptr
  };
};

} // namespace oxygen::examples::common
