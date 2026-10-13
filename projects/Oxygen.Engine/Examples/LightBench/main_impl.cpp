//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

#include "DemoShell/Runtime/DemoMain.h"
#include "DemoShell/Services/SettingsService.h"
#include "LightBench/LightBenchSettings.h"
#include "LightBench/MainModule.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Fluent/OptionValueBuilder.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/RunHost.h>
#include <Oxygen/SceneSync/SceneObserverSyncModule.h>
#include <Oxygen/Vortex/Renderer.h>

#ifdef OXYGEN_BUILD_UI_TESTS
#  include "DemoShell/test/UiTestSession.h"
#endif

namespace examples = oxygen::examples;
namespace host = oxygen::host;
namespace light_bench = oxygen::examples::light_bench;

extern "C" auto MainImpl(std::span<const char*> args) -> int
{
  using oxygen::clap::Option;
  using oxygen::clap::Options;

#ifdef OXYGEN_BUILD_UI_TESTS
  examples::testing::UiTestSession::InitializeSettings();
#endif
  examples::SettingsService::ForDemoApp();

  std::string preset_id = "neutral-reference";
  auto preset = light_bench::LightBenchPreset::kNeutralReference;
  auto options = std::make_shared<Options>("LightBench");
  options->Add(Option::WithKey("preset")
      .About("Initial scenario: neutral-reference, point-falloff, spot-cone, "
             "material-lighting, auto-adaptation, indoor, outdoor-daylight")
      .Long("preset")
      .WithValue<std::string>()
      .DefaultValue(std::string("neutral-reference"))
      .StoreTo(&preset_id)
      .Build());

  return examples::RunDemo(args,
    {
      .program = "light-bench",
      .about = "PBR lighting validation demo",
      .application = "LightBench",
      .options = { { options, false } },
      .configure = [&](const oxygen::clap::CommandLineContext&,
                     const host::HostConfig& config) -> void {
        const auto presets = light_bench::GetPresets();
        const auto selected = std::ranges::find_if(presets,
          [&](const auto& entry) -> bool { return entry.id == preset_id; });
        if (selected == presets.end()) {
          throw std::invalid_argument(
            "Unknown LightBench preset: " + preset_id);
        }
        preset = selected->preset;
        // A batch run must not rewrite the person's settings.
        const bool batch_run
          = config.engine.frame_count != 0U || config.platform.headless;
        examples::SettingsService::ForDemoApp()->SetPersistenceEnabled(
          !batch_run);
        if (batch_run) {
          LOG_F(
            INFO, "LightBench batch run: personal settings writes disabled");
        }
      },
      .register_modules
      = [&](oxygen::AsyncEngine& engine, host::HostContext& context) -> void {
        host::RegisterRequiredModule(
          engine, std::make_unique<light_bench::MainModule>(context, preset));
        host::RegisterRequiredModule(engine,
          std::make_unique<oxygen::scenesync::SceneObserverSyncModule>(
            oxygen::engine::kSceneObserverSyncModulePriority));
        examples::RegisterDemoRenderer(engine, context)
          ->SetGroundGridConfig({ .enabled = false });
      },
    });
}
