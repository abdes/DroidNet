//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include "DemoShell/Internal/PostProcessConsoleBindings.h"
#include "DemoShell/Services/PostProcessSettingsService.h"
#include <fmt/format.h>

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Console/Constants.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/ExposureSettingsStatus.h>
#include <Oxygen/Vortex/Types/ExposureTransition.h>

namespace oxygen::examples::internal {
namespace {
  using Result = console::ExecutionResult;
  using scene::ExposureSettings;
  using Handle = vortex::CompositionView::ViewStateHandle;

  auto Invalid(std::string message) -> Result
  {
    return {
      .status = console::ExecutionStatus::kInvalidArguments,
      .exit_code = console::kExitCodeInvalidArguments,
      .error = std::move(message),
    };
  }
  auto Error(std::string message) -> Result
  {
    return {
      .status = console::ExecutionStatus::kError,
      .exit_code = console::kExitCodeGenericError,
      .error = std::move(message),
    };
  }
  template <typename T> auto Number(std::string_view text, T& result) -> bool
  {
    const auto* begin = std::to_address(text.begin());
    const auto* end = std::to_address(text.end());
    const auto parsed = std::from_chars(begin, end, result);
    return parsed.ec == std::errc {} && parsed.ptr == end;
  }
  auto Scalar(std::string_view text, float& value) -> bool
  {
    if (text.starts_with('+')) {
      text.remove_prefix(1);
    }
    return Number(text, value) && std::isfinite(value);
  }
  constexpr std::array fields {
    std::pair { "manual_ev", &ExposureSettings::manual_ev },
    std::pair { "compensation_ev", &ExposureSettings::compensation_ev },
    std::pair { "key", &ExposureSettings::key },
    std::pair { "min_ev", &ExposureSettings::min_ev },
    std::pair { "max_ev", &ExposureSettings::max_ev },
    std::pair { "speed_up", &ExposureSettings::speed_up },
    std::pair { "speed_down", &ExposureSettings::speed_down },
    std::pair { "low_percentile", &ExposureSettings::low_percentile },
    std::pair { "high_percentile", &ExposureSettings::high_percentile },
    std::pair { "min_log_luminance", &ExposureSettings::min_log_luminance },
    std::pair { "log_luminance_range", &ExposureSettings::log_luminance_range },
    std::pair { "target_luminance", &ExposureSettings::target_luminance },
    std::pair { "spot_meter_radius", &ExposureSettings::spot_meter_radius },
    std::pair { "black_influence", &ExposureSettings::black_influence },
    std::pair { "transition_distance", &ExposureSettings::transition_distance },
  };
  auto Phase(vortex::ExposureTransitionPhase phase) -> const char*
  {
    switch (phase) {
    case vortex::ExposureTransitionPhase::kQueued:
      return "queued";
    case vortex::ExposureTransitionPhase::kApplied:
      return "applied";
    case vortex::ExposureTransitionPhase::kRejected:
      return "rejected";
    case vortex::ExposureTransitionPhase::kSuperseded:
      return "superseded";
    }
    return "unknown";
  }
  auto TransitionError(vortex::ExposureTransitionError error) -> const char*
  {
    using enum vortex::ExposureTransitionError;
    switch (error) {
    case kInvalidTarget:
      return "invalid-target";
    case kRendererUnavailable:
      return "renderer-unavailable";
    case kInvalidPolicy:
      return "invalid-policy";
    case kInvalidSeed:
      return "invalid-seed";
    case kGenerationExhausted:
      return "generation-exhausted";
    case kUnknownToken:
      return "unknown-token";
    case kConflictingToken:
      return "conflicting-token";
    case kNotAuto:
      return "not-auto";
    case kSharedConsumer:
      return "shared-consumer";
    case kUnsupportedSeed:
      return "unsupported-seed";
    case kInvalidDiscontinuity:
      return "invalid-discontinuity";
    }
    return "unknown";
  }
  auto Owner(vortex::Renderer& renderer, std::string_view text)
    -> std::optional<Handle>
  {
    std::uint64_t id = 0;
    if (!Number(text, id)) {
      return std::nullopt;
    }
    const auto handle = Handle { id };
    const auto owners = renderer.GetExposureOwners();
    return std::ranges::find(owners, handle) != owners.end()
      ? std::optional { handle }
      : std::nullopt;
  }
} // namespace

PostProcessConsoleBindings::PostProcessConsoleBindings(
  console::Console& console, ui::PostProcessSettingsService& settings,
  RendererResolver renderer)
  : console_(console)
  , settings_(settings)
  , renderer_(std::move(renderer))
{
  if (!renderer_) {
    throw std::invalid_argument("A renderer resolver is required");
  }
  struct Binding {
    const char* name;
    const char* help;
    Result (PostProcessConsoleBindings::*execute)(const Arguments&);
  };
  const std::array bindings {
    Binding {
      .name = "pp.targets",
      .help = "List the scene:<revision> authoring target and published "
              "exposure owner "
              "handles.",
      .execute = &PostProcessConsoleBindings::Targets,
    },
    Binding {
      .name = "pp.inspect",
      .help
      = "pp.inspect scene:<revision> — authored controls and actual renderer "
        "acceptance status.",
      .execute = &PostProcessConsoleBindings::Inspect,
    },
    Binding {
      .name = "pp.exposure",
      .help
      = "pp.exposure scene:<revision> field=value [...] — atomic exposure "
        "edit; "
        "pp.inspect lists numeric fields. mode=manual|camera|auto; "
        "metering=average|center|spot; enabled=true|false; mask=scene|off.",
      .execute = &PostProcessConsoleBindings::Exposure,
    },
    Binding {
      .name = "pp.camera",
      .help = "pp.camera scene:<revision> aperture_f shutter_rate iso — atomic "
              "physical-camera exposure edit.",
      .execute = &PostProcessConsoleBindings::Camera,
    },
    Binding {
      .name = "pp.output",
      .help
      = "pp.output scene:<revision> none|aces|filmic|reinhard gamma — atomic "
        "output settings.",
      .execute = &PostProcessConsoleBindings::Output,
    },
    Binding {
      .name = "pp.curve",
      .help
      = "pp.curve scene:<revision> clear OR metered_ev:compensation_ev [...] — "
        "replace the complete curve.",
      .execute = &PostProcessConsoleBindings::Curve,
    },
    Binding {
      .name = "pp.transition",
      .help
      = "pp.transition <owner-handle> remeter|preserve OR seed <ev100> — queue "
        "intent; not immediate GPU completion.",
      .execute = &PostProcessConsoleBindings::Transition,
    },
    Binding {
      .name = "pp.transition.status",
      .help
      = "pp.transition.status <owner-handle> — actual renderer token phase, "
        "generation and error.",
      .execute = &PostProcessConsoleBindings::TransitionStatus,
    },
  };
  handles_.reserve(bindings.size());
  bool registered = false;
  const auto rollback = ScopeGuard([&] noexcept -> void {
    if (!registered) {
      Unregister();
    }
  });
  for (const auto& binding : bindings) {
    const auto handle = console_.RegisterCommand({
      .name = binding.name,
      .help = binding.help,
      .flags = console::CommandFlags::kNone,
      .handler = [this, execute = binding.execute](const Arguments& args,
                   const console::CommandContext&) -> Result {
        return (this->*execute)(args);
      },
    });
    if (!handle.IsValid()) {
      throw std::runtime_error(
        std::string("Cannot register console command: ") + binding.name);
    }
    handles_.push_back(handle);
  }
  registered = true;
}

PostProcessConsoleBindings::~PostProcessConsoleBindings() { Unregister(); }
auto PostProcessConsoleBindings::Unregister() -> void
{
  for (const auto handle : handles_) {
    console_.UnregisterCommand(handle);
  }
  handles_.clear();
}
auto PostProcessConsoleBindings::HasTarget(const Arguments& args) const -> bool
{
  if (args.empty()) {
    return false;
  }
  const auto current = settings_.GetAuthoringTarget();
  const auto text = std::string_view(args.front());
  std::uint64_t revision = 0;
  constexpr std::string_view prefix = "scene:";
  return current && text.starts_with(prefix)
    && Number(text.substr(prefix.size()), revision)
    && revision == current->scene_revision;
}
auto PostProcessConsoleBindings::Accepted() const -> Result
{
  return {
    .output = fmt::format("Authored edit accepted: scene:{} epoch={}; renderer "
                          "capture/acceptance is asynchronous (pp.inspect).",
      settings_.GetSceneRevision(), settings_.GetEpoch()),
  };
}
auto PostProcessConsoleBindings::Targets(const Arguments& args) -> Result
{
  if (!args.empty()) {
    return Invalid("pp.targets takes no arguments.");
  }
  std::string output;
  if (const auto target = settings_.GetAuthoringTarget()) {
    output = fmt::format("scene:{} main_view={} (scene-owned settings; views "
                         "may override or share)\n",
      target->scene_revision, target->main_view_id.get());
  } else {
    output = "No active scene authoring target.\n";
  }
  if (const auto renderer = renderer_()) {
    output += "Published exposure owner handles:";
    for (const auto owner : renderer->GetExposureOwners()) {
      output += fmt::format(" {}", owner.get());
    }
  } else {
    output += "Renderer unavailable.";
  }
  return { .output = std::move(output) };
}
auto PostProcessConsoleBindings::Inspect(const Arguments& args) -> Result
{
  if (args.size() != 1 || !HasTarget(args)) {
    return Invalid("Invalid/stale scene target. Use pp.targets.");
  }
  const auto settings = settings_.GetExposureSettings();
  settings_.BindVortexRenderer(renderer_());
  auto text = fmt::format(
    "{} epoch={} enabled={} mode={} metering={} mask={}\n", args.front(),
    settings_.GetEpoch(), settings.enabled, engine::to_string(settings.mode),
    engine::to_string(settings.metering_mode), settings.metering_mask.get());
  for (const auto& [name, member] : fields) {
    text += fmt::format("{}={} ", name, settings.*member);
  }
  text += "\ncurve:";
  for (const auto& key : settings.compensation_curve) {
    text += fmt::format(" {}:{}", key.metered_ev, key.compensation_ev);
  }
  if (const auto camera = settings_.GetCameraExposure()) {
    text
      += fmt::format("\ncamera aperture_f={} shutter_rate={} iso={} ev100={}",
        camera->aperture_f, camera->shutter_rate, camera->iso, camera->GetEv());
  }
  text += fmt::format("\noutput enabled={} tone_mapper={} gamma={}",
    settings_.GetTonemappingEnabled(),
    engine::to_string(settings_.GetToneMapper()), settings_.GetGamma());
  if (const auto active = settings_.GetExposureStatus()) {
    text += fmt::format(
      "\nrenderer revision={} active_settings={} "
      "exposure_matches_authored={} view_override={} shared={} mask={}",
      active->revision, active->active_settings.has_value(),
      active->active_settings && *active->active_settings == settings,
      active->uses_view_override, active->shared_source,
      vortex::to_string(active->mask_status));
    if (active->settings_error) {
      text
        += fmt::format(" error={}", scene::to_string(*active->settings_error));
    }
    if (!active->mask_error.empty()) {
      text += " mask_error=" + active->mask_error;
    }
  } else {
    text += "\nrenderer acceptance unavailable/pending capture";
  }
  return { .output = std::move(text) };
}
auto PostProcessConsoleBindings::Exposure(const Arguments& args) -> Result
{
  if (args.size() < 2 || !HasTarget(args)) {
    return Invalid(
      "Expected scene:<revision> field=value [...]; use pp.targets.");
  }
  auto next = settings_.GetExposureSettings();
  std::unordered_set<std::string_view> seen;
  for (std::size_t i = 1; i < args.size(); ++i) {
    const auto text = std::string_view(args.at(i));
    const auto separator = text.find('=');
    if (separator == std::string_view::npos) {
      return Invalid("Expected field=value.");
    }
    const auto field = text.substr(0, separator);
    const auto value = text.substr(separator + 1);
    if (!seen.insert(field).second) {
      return Invalid("Duplicate exposure field: " + std::string(field));
    }
    if (field == "enabled") {
      if (value != "true" && value != "false") {
        return Invalid("enabled expects true|false.");
      }
      next.enabled = value == "true";
    } else if (field == "mode") {
      if (value == "auto") {
        next.mode = engine::ExposureMode::kAuto;
      } else if (value == "manual") {
        next.mode = engine::ExposureMode::kManual;
      } else if (value == "camera") {
        next.mode = engine::ExposureMode::kManualCamera;
      } else {
        return Invalid("mode expects manual|camera|auto.");
      }
    } else if (field == "metering") {
      if (value == "average") {
        next.metering_mode = engine::MeteringMode::kAverage;
      } else if (value == "center") {
        next.metering_mode = engine::MeteringMode::kCenterWeighted;
      } else if (value == "spot") {
        next.metering_mode = engine::MeteringMode::kSpot;
      } else {
        return Invalid("metering expects average|center|spot.");
      }
    } else if (field == "mask") {
      if (value == "off") {
        next.metering_mask = {};
      } else if (value == "scene" && settings_.HasSceneMeteringMask()) {
        next.metering_mask = settings_.GetSceneMeteringMask();
      } else {
        return Invalid("mask expects off or an available scene mask.");
      }
    } else {
      const auto found = std::ranges::find_if(fields,
        [field](const auto& item) -> auto { return field == item.first; });
      if (found == fields.end() || !Scalar(value, next.*found->second)) {
        return Invalid(
          "Unknown field or invalid finite number: " + std::string(field));
      }
    }
  }
  return settings_.TrySetExposureSettings(next)
    ? Accepted()
    : Invalid(std::string(settings_.GetValidationError()));
}
auto PostProcessConsoleBindings::Camera(const Arguments& args) -> Result
{
  scene::CameraExposure next;
  if (args.size() != 4 || !HasTarget(args)
    || !Scalar(args.at(1), next.aperture_f)
    || !Scalar(args.at(2), next.shutter_rate)
    || !Scalar(args.at(3), next.iso)) {
    return Invalid("Expected scene:<revision> aperture_f shutter_rate iso.");
  }
  return settings_.TrySetCameraExposure(next)
    ? Accepted()
    : Invalid(std::string(settings_.GetValidationError()));
}
auto PostProcessConsoleBindings::Output(const Arguments& args) -> Result
{
  float gamma = 0;
  if (args.size() != 3 || !HasTarget(args) || !Scalar(args.at(2), gamma)) {
    return Invalid(
      "Expected scene:<revision> none|aces|filmic|reinhard gamma.");
  }
  auto mode = engine::ToneMapper::kNone;
  if (args.at(1) == "none") {
    mode = engine::ToneMapper::kNone;
  } else if (args.at(1) == "aces") {
    mode = engine::ToneMapper::kAcesFitted;
  } else if (args.at(1) == "filmic") {
    mode = engine::ToneMapper::kFilmic;
  } else if (args.at(1) == "reinhard") {
    mode = engine::ToneMapper::kReinhard;
  } else {
    return Invalid("Unknown tone mapper.");
  }
  return settings_.TrySetOutputSettings(mode, gamma)
    ? Accepted()
    : Invalid(std::string(settings_.GetValidationError()));
}
auto PostProcessConsoleBindings::Curve(const Arguments& args) -> Result
{
  if (args.size() < 2 || !HasTarget(args)) {
    return Invalid("Expected scene:<revision> clear OR ev:compensation [...].");
  }
  std::vector<scene::ExposureCompensationKey> keys;
  if (args.size() != 2 || args.at(1) != "clear") {
    if (args.size() - 1 > engine::kMaxExposureCompensationCurveKeys) {
      return Invalid("At most 64 curve keys are supported.");
    }
    for (std::size_t i = 1; i < args.size(); ++i) {
      const auto text = std::string_view(args.at(i));
      const auto separator = text.find(':');
      scene::ExposureCompensationKey key;
      if (separator == std::string_view::npos
        || !Scalar(text.substr(0, separator), key.metered_ev)
        || !Scalar(text.substr(separator + 1), key.compensation_ev)) {
        return Invalid(
          "Each curve key must be finite metered_ev:compensation_ev.");
      }
      keys.push_back(key);
    }
  }
  return settings_.SetExposureCompensationCurve(keys)
    ? Accepted()
    : Invalid(std::string(settings_.GetValidationError()));
}
auto PostProcessConsoleBindings::Transition(const Arguments& args) -> Result
{
  if (args.size() < 2) {
    return Invalid("Expected owner-handle remeter|preserve OR seed ev100.");
  }
  const auto renderer = renderer_();
  if (!renderer) {
    return Error("Renderer unavailable.");
  }
  const auto owner = Owner(*renderer, args.front());
  if (!owner) {
    return Invalid("Invalid/stale exposure owner. Use pp.targets; borrowers "
                   "are not owners.");
  }
  auto policy = vortex::ExposureTransitionPolicy::kRemeter;
  std::optional<float> seed;
  if (args.at(1) == "seed" && args.size() == 3) {
    float ev = 0;
    if (!Scalar(args.at(2), ev)) {
      return Invalid("Seed EV100 must be finite.");
    }
    seed = ev;
    policy = vortex::ExposureTransitionPolicy::kSeedFromEv100;
  } else if (args.at(1) == "preserve" && args.size() == 2) {
    policy = vortex::ExposureTransitionPolicy::kPreserve;
  } else if (args.at(1) != "remeter" || args.size() != 2) {
    return Invalid("Expected remeter|preserve OR seed ev100.");
  }
  const auto token = renderer->QueueExposureTransition(*owner, policy, seed);
  if (!token) {
    return Error(TransitionError(token.error()));
  }
  return {
    .output = fmt::format("queued owner={} lifetime={} generation={}; "
                          "use pp.transition.status for completion",
      token->target.get(), token->lifetime, token->generation),
  };
}
auto PostProcessConsoleBindings::TransitionStatus(const Arguments& args)
  -> Result
{
  if (args.size() != 1) {
    return Invalid("Expected one owner handle.");
  }
  const auto renderer = renderer_();
  if (!renderer) {
    return Error("Renderer unavailable.");
  }
  const auto owner = Owner(*renderer, args.front());
  if (!owner) {
    return Invalid("Invalid/stale exposure owner. Use pp.targets.");
  }
  const auto status = renderer->InspectExposureTransition(*owner);
  if (!status) {
    return { .output = "No transition has been requested for this owner." };
  }
  return {
    .output = fmt::format(
      "{} owner={} lifetime={} generation={} applied_generation={} error={}",
      Phase(status->phase), owner->get(), status->request.lifetime,
      status->request.generation, status->applied_generation,
      status->error ? TransitionError(*status->error) : "none"),
  };
}

} // namespace oxygen::examples::internal
