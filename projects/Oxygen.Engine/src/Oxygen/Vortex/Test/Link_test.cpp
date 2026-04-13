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
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Time/SimulationClock.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Vortex/Renderer.h>

#include "Fakes/Graphics.h"

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

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
      if (!std::string_view { line }.contains(kLegacyIncludeNeedle)) {
        continue;
      }

      throw std::runtime_error("legacy include seam found at "
        + entry.path().generic_string() + ':' + std::to_string(line_number));
    }
  }
}

auto VerifyBoundaryContracts() -> void
{
  namespace fs = std::filesystem;

  const auto root = fs::path { OXYGEN_VORTEX_SOURCE_DIR };
  const auto verify_absent = [](const fs::path& file, std::string_view needle) {
    auto input = std::ifstream(file);
    auto line = std::string {};
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
      ++line_number;
      if (!std::string_view { line }.contains(needle)) {
        continue;
      }

      throw std::runtime_error("forbidden Phase 1 boundary token found at "
        + file.generic_string() + ':' + std::to_string(line_number) + " -> "
        + std::string(needle));
    }
  };

  verify_absent(root / "Renderer.h", "Internal/RenderContextPool.h");
  verify_absent(root / "CMakeLists.txt", "SceneRenderer/ShaderDebugMode.h");
  verify_absent(root / "CMakeLists.txt", "SceneRenderer/ShaderPassConfig.h");
  verify_absent(root / "CMakeLists.txt", "SceneRenderer/ToneMapPassConfig.h");
  verify_absent(root / "CMakeLists.txt", "oxygen::imgui");
}

auto RunFrameHooks(Renderer& renderer, FrameContext& frame_context) -> void
{
  frame_context.SetFrameSlot(oxygen::frame::Slot { 0U },
    oxygen::engine::internal::EngineTagFactory::Get());
  frame_context.SetFrameSequenceNumber(oxygen::frame::SequenceNumber { 1U },
    oxygen::engine::internal::EngineTagFactory::Get());
  frame_context.SetModuleTimingData(
    oxygen::engine::ModuleTimingData {
      .game_delta_time = oxygen::time::SimulationClock::kMinDeltaTime,
      .fixed_delta_time = oxygen::time::SimulationClock::kMinDeltaTime,
      .time_scale = 1.0F,
      .is_paused = false,
      .interpolation_alpha = 0.0F,
      .current_fps = 60.0F,
      .fixed_steps_this_frame = 1U,
    },
    oxygen::engine::internal::EngineTagFactory::Get());

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
    VerifyBoundaryContracts();

    auto graphics = std::make_shared<FakeGraphics>();
    graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

    auto config = RendererConfig {};
    config.upload_queue_key = graphics->QueueKeyFor(QueueRole::kGraphics).get();

    auto renderer = Renderer(
      std::weak_ptr<Graphics>(graphics), std::move(config), CapabilitySet {});
    auto frame_context = FrameContext {};

    RunFrameHooks(renderer, frame_context);
    renderer.OnShutdown();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
  } catch (...) {
    std::cerr << "Oxygen.Vortex.LinkTest failed with an unknown exception\n";
  }

  return 1;
}
