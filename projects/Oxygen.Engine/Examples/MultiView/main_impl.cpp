//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

#include "DemoShell/Runtime/DemoMain.h"
#include "DemoShell/Services/SettingsService.h"
#include "MultiView/CompositingMode.h"
#include "MultiView/MainModule.h"
#include "MultiView/SceneBootstrapper.h"

#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Fluent/OptionValueBuilder.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Host/HostConfig.h>
#include <Oxygen/Host/MainViewOptions.h>
#include <Oxygen/Host/RunHost.h>

namespace examples = oxygen::examples;
namespace host = oxygen::host;

extern "C" auto MainImpl(std::span<const char*> args) -> int
{
  using oxygen::clap::Option;
  using oxygen::clap::Options;

  examples::SettingsService::ForDemoApp();

  std::string compositing_mode_value = "copy";
  bool pip_wireframe = true;
  uint32_t pip_scissor_inset_px = 0U;
  bool proof_layout = false;
  bool aux_proof_layout = false;
  bool offscreen_proof_layout = false;
  bool feature_variant_proof_layout = false;
  bool proof_wireframe_overlay = false;
  bool point_light_enabled = true;
  bool spot_light_enabled = true;
  bool directional_array_proof = false;
  std::string exposure_proof_value = "none";
  std::string exposure_view_only;
  std::string visual_fog_value = "volume";
  bool visual_fog_cycle = false;
  bool visual_fog_hold_local = false;
  bool visual_fog_jitter = true;
  auto main_module_config = examples::multiview::MainModuleConfig {};

  auto options = std::make_shared<Options>("MultiView");
  options->Add(Option::WithKey("composite-mode")
      .About("Compositing mode to use: copy or blend")
      .Short("c")
      .Long("composite-mode")
      .WithValue<std::string>()
      .DefaultValue("copy")
      .UserFriendlyName("mode")
      .StoreTo(&compositing_mode_value)
      .Build());
  options->Add(Option::WithKey("exposure-proof")
      .About("Exposure proof: none, independent, shared, main-only, "
             "pip-only, reordered, source-loss, viewport, lifetime, "
             "window-resize, modes, atmosphere, atmosphere-lit, "
             "consumer-visual, layouts, mixed, interactions")
      .Long("exposure-proof")
      .WithValue<std::string>()
      .DefaultValue("none")
      .StoreTo(&exposure_proof_value)
      .Build());
  options->Add(Option::WithKey("exposure-view-only")
      .About("Layouts proof: render one named view and its required "
             "producers")
      .Long("exposure-view-only")
      .WithValue<std::string>()
      .DefaultValue("")
      .StoreTo(&exposure_view_only)
      .Build());
  options->Add(Option::WithKey("visual-fog")
      .About("Initial consumer-visual comparison: clear, volume, or local")
      .Long("visual-fog")
      .WithValue<std::string>()
      .DefaultValue("volume")
      .StoreTo(&visual_fog_value)
      .Build());
  options->Add(Option::WithKey("visual-fog-cycle")
      .About("Script the visual controls: clear, volume at 32, local "
             "at 96, clear at 128")
      .Long("visual-fog-cycle")
      .WithValue<bool>()
      .DefaultValue(false)
      .StoreTo(&visual_fog_cycle)
      .Build());
  options->Add(Option::WithKey("visual-fog-hold-local")
      .About("Run the visual fog cycle and leave Local fog active for "
             "temporal diagnosis")
      .Long("visual-fog-hold-local")
      .WithValue<bool>()
      .DefaultValue(false)
      .StoreTo(&visual_fog_hold_local)
      .Build());
  options->Add(Option::WithKey("visual-fog-jitter")
      .About("Volumetric jitter for the bounded consumer-visual comparison")
      .Long("visual-fog-jitter")
      .WithValue<bool>()
      .DefaultValue(true)
      .StoreTo(&visual_fog_jitter)
      .Build());
  options->Add(Option::WithKey("proof-wireframe-overlay")
      .About("Exercise per-frame wireframe overlays in the four-view "
             "proof layout")
      .Long("proof-wireframe-overlay")
      .WithValue<bool>()
      .DefaultValue(false)
      .StoreTo(&proof_wireframe_overlay)
      .Build());
  options->Add(Option::WithKey("directional-array-proof")
      .About("Validate one unassigned and two atmospheric directional "
             "lights")
      .Long("directional-array-proof")
      .WithValue<bool>()
      .DefaultValue(false)
      .StoreTo(&directional_array_proof)
      .Build());
  options->Add(Option::WithKey("point-light")
      .About("Enable the point fill light; disable to isolate the spotlight")
      .Long("point-light")
      .WithValue<bool>()
      .DefaultValue(true)
      .StoreTo(&point_light_enabled)
      .Build());
  options->Add(Option::WithKey("spot-light")
      .About("Enable the key spotlight; disable to isolate the point light")
      .Long("spot-light")
      .WithValue<bool>()
      .DefaultValue(true)
      .StoreTo(&spot_light_enabled)
      .Build());
  options->Add(Option::WithKey("pip-wireframe")
      .About("Render the PiP scene view in wireframe mode")
      .Long("pip-wireframe")
      .WithValue<bool>()
      .DefaultValue(true)
      .UserFriendlyName("enabled")
      .StoreTo(&pip_wireframe)
      .Build());
  options->Add(Option::WithKey("pip-scissor-inset")
      .About("Inset the PiP scene scissor in pixels before rendering")
      .Long("pip-scissor-inset")
      .WithValue<uint32_t>()
      .DefaultValue(0U)
      .UserFriendlyName("pixels")
      .StoreTo(&pip_scissor_inset_px)
      .Build());
  options->Add(Option::WithKey("proof-layout")
      .About("Use the four-view M06A validation layout")
      .Long("proof-layout")
      .WithValue<bool>()
      .DefaultValue(false)
      .UserFriendlyName("enabled")
      .StoreTo(&proof_layout)
      .Build());
  options->Add(Option::WithKey("aux-proof-layout")
      .About("Use the M06A auxiliary producer/consumer validation layout")
      .Long("aux-proof-layout")
      .WithValue<bool>()
      .DefaultValue(false)
      .UserFriendlyName("enabled")
      .StoreTo(&aux_proof_layout)
      .Build());
  options->Add(Option::WithKey("offscreen-proof-layout")
      .About("Use the M06B offscreen preview/capture validation layout")
      .Long("offscreen-proof-layout")
      .WithValue<bool>()
      .DefaultValue(false)
      .UserFriendlyName("enabled")
      .StoreTo(&offscreen_proof_layout)
      .Build());
  options->Add(Option::WithKey("feature-variant-proof-layout")
      .About("Use the M06C feature-variant validation layout")
      .Long("feature-variant-proof-layout")
      .WithValue<bool>()
      .DefaultValue(false)
      .UserFriendlyName("enabled")
      .StoreTo(&feature_variant_proof_layout)
      .Build());

  return examples::RunDemo(args,
    {
      .program = "multi-view",
      .about = "Multi-view rendering demo",
      .application = "MultiView Example",
      .options = { { options, false } },
      .configure = [&](const oxygen::clap::CommandLineContext&,
                     host::HostConfig& config) -> void {
        main_module_config = oxygen::examples::multiview::MainModuleConfig {
          .compositing_mode
          = oxygen::examples::multiview::CompositingMode::kBlend,
          .pip_force_wireframe = pip_wireframe,
          .pip_scissor_inset_px = pip_scissor_inset_px,
          .proof_layout = proof_layout,
          .aux_proof_layout = aux_proof_layout,
          .offscreen_proof_layout = offscreen_proof_layout,
          .feature_variant_proof_layout = feature_variant_proof_layout,
          .proof_wireframe_overlay = proof_wireframe_overlay,
          .point_light_enabled = point_light_enabled,
          .spot_light_enabled = spot_light_enabled,
          .directional_array_proof = directional_array_proof,
        };
        using ExposureProof
          = oxygen::examples::multiview::ExposureProofScenario;
        if (proof_wireframe_overlay && !proof_layout && !aux_proof_layout) {
          throw std::invalid_argument(
            "Wireframe overlay proof requires a four-view proof layout");
        }
        if (exposure_proof_value == "independent") {
          main_module_config.exposure_proof = ExposureProof::kIndependent;
        } else if (exposure_proof_value == "shared") {
          main_module_config.exposure_proof = ExposureProof::kShared;
        } else if (exposure_proof_value == "main-only") {
          main_module_config.exposure_proof = ExposureProof::kMainOnly;
        } else if (exposure_proof_value == "pip-only") {
          main_module_config.exposure_proof = ExposureProof::kPipOnly;
        } else if (exposure_proof_value == "reordered") {
          main_module_config.exposure_proof = ExposureProof::kReordered;
        } else if (exposure_proof_value == "source-loss") {
          main_module_config.exposure_proof = ExposureProof::kSourceLoss;
        } else if (exposure_proof_value == "viewport") {
          main_module_config.exposure_proof = ExposureProof::kViewport;
        } else if (exposure_proof_value == "lifetime") {
          main_module_config.exposure_proof = ExposureProof::kLifetime;
        } else if (exposure_proof_value == "window-resize") {
          main_module_config.exposure_proof = ExposureProof::kWindowResize;
        } else if (exposure_proof_value == "modes") {
          main_module_config.exposure_proof = ExposureProof::kModes;
        } else if (exposure_proof_value == "atmosphere") {
          main_module_config.exposure_proof = ExposureProof::kAtmosphere;
        } else if (exposure_proof_value == "atmosphere-lit") {
          main_module_config.exposure_proof = ExposureProof::kAtmosphereLit;
        } else if (exposure_proof_value == "consumer-visual") {
          main_module_config.exposure_proof = ExposureProof::kConsumerVisual;
        } else if (exposure_proof_value == "layouts") {
          main_module_config.exposure_proof = ExposureProof::kLayouts;
        } else if (exposure_proof_value == "mixed") {
          main_module_config.exposure_proof = ExposureProof::kMixed;
        } else if (exposure_proof_value == "interactions") {
          main_module_config.exposure_proof = ExposureProof::kInteractions;
        } else if (exposure_proof_value != "none") {
          throw std::invalid_argument("Unknown exposure proof scenario");
        }
        if (!exposure_view_only.empty()
          && main_module_config.exposure_proof != ExposureProof::kLayouts
          && main_module_config.exposure_proof != ExposureProof::kMixed) {
          throw std::invalid_argument(
            "Exposure view selection requires layouts or mixed proof");
        }
        main_module_config.exposure_view_only = exposure_view_only;
        using VisualFog = oxygen::examples::multiview::VisualFogMode;
        main_module_config.visual_fog_jitter = visual_fog_jitter;
        if (visual_fog_value == "clear") {
          main_module_config.visual_fog_mode = VisualFog::kClear;
        } else if (visual_fog_value == "local") {
          main_module_config.visual_fog_mode = VisualFog::kLocal;
        } else if (visual_fog_value != "volume") {
          throw std::invalid_argument("Unknown visual fog comparison");
        }
        if (visual_fog_cycle || visual_fog_hold_local) {
          if (main_module_config.exposure_proof
            != ExposureProof::kConsumerVisual) {
            throw std::invalid_argument(
              "Visual fog cycling requires consumer-visual");
          }
          main_module_config.visual_fog_cycle = true;
          main_module_config.visual_fog_hold_local = visual_fog_hold_local;
          main_module_config.visual_fog_mode = VisualFog::kClear;
        }
        if ((main_module_config.exposure_proof == ExposureProof::kWindowResize
              || main_module_config.exposure_proof
                == ExposureProof::kInteractions)
          && (config.platform.headless || config.window.fullscreen)) {
          throw std::invalid_argument(
            "This proof requires a windowed presentation surface");
        }
        if (main_module_config.exposure_proof != ExposureProof::kNone
          && main_module_config.exposure_proof != ExposureProof::kLayouts
          && main_module_config.exposure_proof != ExposureProof::kMixed
          && main_module_config.exposure_proof != ExposureProof::kInteractions
          && (proof_layout || aux_proof_layout || offscreen_proof_layout
            || feature_variant_proof_layout)) {
          throw std::invalid_argument(
            "Exposure proof requires the ordinary main/PiP layout");
        }
        if (main_module_config.exposure_proof == ExposureProof::kInteractions
          && (proof_layout || aux_proof_layout
            || feature_variant_proof_layout)) {
          throw std::invalid_argument(
            "Interactions require ordinary or offscreen layout");
        }
        auto mode_lower = compositing_mode_value;
        std::ranges::transform(
          mode_lower, mode_lower.begin(), [](unsigned char c) -> char {
            return static_cast<char>(std::tolower(c));
          });
        if (mode_lower == "copy") {
          main_module_config.compositing_mode
            = oxygen::examples::multiview::CompositingMode::kCopy;
        } else if (mode_lower == "blend") {
          main_module_config.compositing_mode
            = oxygen::examples::multiview::CompositingMode::kBlend;
        } else if (!mode_lower.empty()) {
          throw std::runtime_error(
            "Invalid composite mode; use 'copy' or 'blend'");
        }
        if (proof_wireframe_overlay) {
          // The main view takes the host's view mode; the others take the
          // overlay from the module.
          config.startup_cvars.Set(
            std::string(host::kViewModeCVar), "lit-wireframe");
        }
      },
      .register_modules
      = [&](oxygen::AsyncEngine& engine, host::HostContext& context) -> void {
        host::RegisterRequiredModule(engine,
          std::make_unique<examples::multiview::MainModule>(
            context, main_module_config));
        examples::RegisterDemoRenderer(engine, context);
      },
    });
}
