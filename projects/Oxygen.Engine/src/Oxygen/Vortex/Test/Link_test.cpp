//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

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
using oxygen::observer_ptr;
using oxygen::RendererConfig;
using oxygen::engine::FrameContext;
using oxygen::graphics::QueueRole;
using oxygen::vortex::CapabilitySet;
using oxygen::vortex::Renderer;
using oxygen::vortex::testing::FakeGraphics;

auto VerifySourceHermeticity() -> void
{
  namespace fs = std::filesystem;

  constexpr auto kLegacyIncludeNeedle = std::string_view { "Oxygen/"
                                                           "Renderer/" };
  const auto root = fs::path { OXYGEN_VORTEX_SOURCE_DIR };

  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    auto file = std::ifstream(entry.path());
    auto line = std::string {};
    std::size_t line_number = 0;
    while (std::getline(file, line)) {
      ++line_number;
      if (std::string_view { line }.find(kLegacyIncludeNeedle)
        == std::string_view::npos) {
        continue;
      }

      throw std::runtime_error("legacy include seam found at "
        + entry.path().generic_string() + ':' + std::to_string(line_number));
    }
  }
}

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
    VerifySourceHermeticity();

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
