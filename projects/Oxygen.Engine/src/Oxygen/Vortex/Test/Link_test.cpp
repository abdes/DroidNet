//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <iostream>
#include <memory>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Vortex/Renderer.h>

#include "Fakes/Graphics.h"

namespace {

using oxygen::Graphics;
using oxygen::RendererConfig;
using oxygen::engine::FrameContext;
using oxygen::graphics::QueueRole;
using oxygen::observer_ptr;
using oxygen::vortex::CapabilitySet;
using oxygen::vortex::Renderer;
using oxygen::vortex::testing::FakeGraphics;

auto RunFrameHooks(Renderer& renderer, FrameContext& frame_context) -> void
{
  const auto frame = observer_ptr<FrameContext> { &frame_context };

  renderer.OnFrameStart(frame);

  auto loop = oxygen::co::testing::TestEventLoop {};
  oxygen::co::Run(loop, [&]() -> oxygen::co::Co<void> {
    co_await renderer.OnTransformPropagation(frame);
    co_await renderer.OnPreRender(frame);
    co_await renderer.OnRender(frame);
    co_await renderer.OnCompositing(frame);
  });

  renderer.OnFrameEnd(frame);
}

} // namespace

auto main() -> int
{
  try {
    auto graphics = std::make_shared<FakeGraphics>();
    graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

    auto config = RendererConfig {};
    config.upload_queue_key = graphics->QueueKeyFor(QueueRole::kGraphics).get();

    auto renderer = Renderer(
      std::weak_ptr<Graphics>(graphics), std::move(config), CapabilitySet {});
    auto frame_context = FrameContext {};

    RunFrameHooks(renderer, frame_context);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
  } catch (...) {
    std::cerr << "Oxygen.Vortex.LinkTest failed with an unknown exception\n";
  }

  return 1;
}
