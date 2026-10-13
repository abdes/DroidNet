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
#include <utility>
#include <vector>

#include "DemoShell/Internal/PostProcessConsoleBindings.h"
#include "DemoShell/Services/PostProcessSettingsService.h"
#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Console/Parser.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/ExposureSettingsStatus.h>
#include <Oxygen/Vortex/Types/ExposureTransition.h>

namespace oxygen::examples::internal {
namespace {
  using Result = commands::CommandResult;
  using scene::ExposureSettings;
  using Handle = vortex::CompositionView::ViewStateHandle;

  auto Invalid(std::string message) -> Result
  {
    return {
      .status = commands::ExecutionStatus::kInvalidArguments,
      .error = std::move(message),
    };
  }
  auto Error(std::string message) -> Result
  {
    return {
      .status = commands::ExecutionStatus::kError,
      .error = std::move(message),
    };
  }

  template <typename T> using NamedValue = std::pair<std::string_view, T>;

  constexpr auto kExposureModes
    = std::to_array<NamedValue<engine::ExposureMode>>({
      { "manual", engine::ExposureMode::kManual },
      { "camera", engine::ExposureMode::kManualCamera },
      { "auto", engine::ExposureMode::kAuto },
    });
  constexpr auto kMeteringModes
    = std::to_array<NamedValue<engine::MeteringMode>>({
      { "average", engine::MeteringMode::kAverage },
      { "center", engine::MeteringMode::kCenterWeighted },
      { "spot", engine::MeteringMode::kSpot },
    });
  constexpr auto kToneCurves = std::to_array<NamedValue<engine::ToneMapper>>({
    { "none", engine::ToneMapper::kNone },
    { "aces", engine::ToneMapper::kAcesFitted },
    { "filmic", engine::ToneMapper::kFilmic },
    { "reinhard", engine::ToneMapper::kReinhard },
  });

  template <typename T, std::size_t N>
  auto NamesOf(const std::array<NamedValue<T>, N>& table)
    -> std::vector<std::string>
  {
    auto names = std::vector<std::string> {};
    names.reserve(N);
    for (const auto& [name, value] : table) {
      static_cast<void>(value);
      names.emplace_back(name);
    }
    return names;
  }

  //! The value named `name`; the binder has already checked the name.
  template <typename T, std::size_t N>
  auto ValueOf(const std::array<NamedValue<T>, N>& table, std::string_view name)
    -> T
  {
    const auto it = std::ranges::find(table, name, &NamedValue<T>::first);
    CHECK_F(it != table.end(), "unknown enum value '{}'", name);
    return it->second;
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
  auto Owner(vortex::Renderer& renderer, const std::int64_t id)
    -> std::optional<Handle>
  {
    if (id < 0) {
      return std::nullopt;
    }
    const auto handle = Handle { static_cast<std::uint64_t>(id) };
    const auto owners = renderer.GetExposureOwners();
    return std::ranges::find(owners, handle) != owners.end()
      ? std::optional { handle }
      : std::nullopt;
  }

  constexpr const char* kTarget = "target";
  constexpr const char* kOwner = "owner";

  auto StaleTarget() -> Result
  {
    return Invalid("Invalid or stale scene target; use pp.targets.");
  }

  auto TargetParam() -> commands::CommandParam
  {
    return {
      .name = kTarget,
      .type = commands::ParamType::kString,
      .help = "scene:<revision> from pp.targets",
      .required = true,
    };
  }

  auto OwnerParam() -> commands::CommandParam
  {
    return {
      .name = kOwner,
      .type = commands::ParamType::kInt,
      .help = "Exposure owner handle from pp.targets",
      .required = true,
      .min_value = 0.0,
    };
  }

  auto NumberParam(std::string name, std::string help) -> commands::CommandParam
  {
    return {
      .name = std::move(name),
      .type = commands::ParamType::kDouble,
      .help = std::move(help),
      .required = true,
    };
  }

  auto BoolParam(std::string name, std::string help) -> commands::CommandParam
  {
    return {
      .name = std::move(name),
      .type = commands::ParamType::kBool,
      .help = std::move(help),
      .required = true,
    };
  }

  auto EnumParam(std::string name, std::string help,
    std::vector<std::string> values) -> commands::CommandParam
  {
    return {
      .name = std::move(name),
      .type = commands::ParamType::kEnum,
      .help = std::move(help),
      .required = true,
      .allowed_values = std::move(values),
    };
  }

  auto Optional(commands::CommandParam param) -> commands::CommandParam
  {
    param.required = false;
    return param;
  }

  auto FieldHelp(const std::string_view name) -> std::string
  {
    constexpr std::array help {
      std::pair { "manual_ev", "Manual exposure value, EV100" },
      std::pair { "compensation_ev", "Added exposure in EV" },
      std::pair { "key", "Display calibration multiplier; reference 12.5" },
      std::pair { "min_ev", "Lowest metered EV100 of automatic exposure" },
      std::pair { "max_ev", "Highest metered EV100 of automatic exposure" },
      std::pair { "speed_up", "Adaptation toward brighter scenes, EV/s" },
      std::pair { "speed_down", "Adaptation toward darker scenes, EV/s" },
      std::pair { "low_percentile", "Histogram low percentile, a fraction" },
      std::pair { "high_percentile", "Histogram high percentile, a fraction" },
      std::pair {
        "min_log_luminance", "Histogram lower bound, log2 luminance" },
      std::pair { "log_luminance_range", "Histogram range, log2 luminance" },
      std::pair { "target_luminance", "Reference luminance of auto exposure" },
      std::pair {
        "spot_meter_radius", "Spot radius in normalized coordinates" },
      std::pair { "black_influence", "Weight of black samples, a fraction" },
      std::pair { "transition_distance",
        "EV from the target where adaptation turns smooth" },
    };
    const auto found = std::ranges::find_if(
      help, [name](const auto& entry) -> bool { return entry.first == name; });
    return found == help.end() ? std::string(name) : found->second;
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
  bool registered = false;
  const auto rollback = ScopeGuard([&] noexcept -> void {
    if (!registered) {
      Unregister();
    }
  });
  const auto handler = [this](Result (PostProcessConsoleBindings::*execute)(
                         const Args&)) -> commands::CommandHandler {
    return [this, execute](
             const Args& args, const commands::CommandContext&) -> Result {
      return (this->*execute)(args);
    };
  };

  Register({
    .name = "pp.targets",
    .label = "Post-Process Targets",
    .help = "List the scene:<revision> authoring target and the published "
            "exposure owner handles",
    .handler = [this](const Args&, const commands::CommandContext&) -> Result {
      return Targets();
    },
  });
  Register({
    .name = "pp.inspect",
    .label = "Inspect Post-Process",
    .help = "Authored exposure and output settings, and whether the renderer "
            "accepted them",
    .params = { TargetParam() },
    .handler = handler(&PostProcessConsoleBindings::Inspect),
  });
  {
    // Every field is optional; the edit changes only the fields given and
    // applies all of them or none.
    std::vector<commands::CommandParam> params { TargetParam() };
    params.push_back(Optional(BoolParam("enabled", "Apply exposure")));
    params.push_back(Optional(EnumParam("mode",
      "manual EV100, physical camera or automatic", NamesOf(kExposureModes))));
    params.push_back(Optional(EnumParam(
      "metering", "How the image is metered", NamesOf(kMeteringModes))));
    params.push_back(Optional(EnumParam("mask",
      "Meter through the scene's metering mask, or not", { "scene", "off" })));
    for (const auto& [name, member] : fields) {
      static_cast<void>(member);
      params.push_back(Optional(NumberParam(name, FieldHelp(name))));
    }
    Register({
      .name = "pp.exposure",
      .label = "Exposure",
      .help = "Edit exposure fields atomically: every field given or none",
      .params = std::move(params),
      .handler = handler(&PostProcessConsoleBindings::Exposure),
    });
  }
  Register({
    .name = "pp.camera",
    .label = "Physical Camera Exposure",
    .help = "Set the physical camera's aperture, shutter rate and ISO together",
    .params = {
      TargetParam(),
      NumberParam("aperture_f", "Aperture f-number"),
      NumberParam("shutter_rate", "Shutter rate in 1/s"),
      NumberParam("iso", "Sensor sensitivity"),
    },
    .handler = handler(&PostProcessConsoleBindings::Camera),
  });
  Register({
    .name = "pp.output",
    .label = "Display Output",
    .help = "Set the tone curve and the display gamma together",
    .params = {
      TargetParam(),
      EnumParam("tone_curve", "How HDR brightness maps to the display",
        NamesOf(kToneCurves)),
      NumberParam("gamma", "Display gamma applied after the tone curve"),
    },
    .handler = handler(&PostProcessConsoleBindings::Output),
  });
  {
    auto keys = commands::CommandParam {
      .name = "keys",
      .type = commands::ParamType::kString,
      .help = "clear, or metered_ev:compensation_ev pairs ordered by metered "
              "EV (at most 64)",
      .required = true,
      .rest_of_line = true,
    };
    Register({
      .name = "pp.curve",
      .label = "Compensation Curve",
      .help = "Replace the whole exposure compensation curve",
      .params = { TargetParam(), std::move(keys) },
      .handler = handler(&PostProcessConsoleBindings::Curve),
    });
  }
  Register({
    .name = "pp.reset_auto",
    .label = "Reset Auto Exposure",
    .help = "Reset the automatic exposure controls; mode, compensation, "
            "calibration and camera settings are kept",
    .params = { TargetParam() },
    .handler = handler(&PostProcessConsoleBindings::ResetAuto),
  });
  Register({
    .name = "pp.transition",
    .label = "Exposure Transition",
    .help = "Queue an exposure transition for an owner; its completion is "
            "reported by pp.transition.status",
    .params = {
      OwnerParam(),
      EnumParam("policy",
        "remeter, preserve, or seed from the given EV100",
        { "remeter", "preserve", "seed" }),
      Optional(NumberParam("ev100", "Seed EV100; required with seed")),
    },
    .handler = handler(&PostProcessConsoleBindings::Transition),
  });
  Register({
    .name = "pp.transition.status",
    .label = "Exposure Transition Status",
    .help = "Phase, generation and error of an owner's latest transition",
    .params = { OwnerParam() },
    .handler = handler(&PostProcessConsoleBindings::TransitionStatus),
  });
  registered = true;
}

PostProcessConsoleBindings::~PostProcessConsoleBindings() { Unregister(); }

auto PostProcessConsoleBindings::Register(
  commands::CommandDefinition definition) -> void
{
  const auto name = definition.name;
  const auto handle = console_.Commands()->Register(std::move(definition));
  if (!handle.IsValid()) {
    throw std::runtime_error("Cannot register console command: " + name);
  }
  handles_.push_back(handle);
}

auto PostProcessConsoleBindings::Unregister() -> void
{
  for (const auto handle : handles_) {
    console_.Commands()->Unregister(handle);
  }
  handles_.clear();
}

auto PostProcessConsoleBindings::IsCurrentTarget(
  const std::string_view target) const -> bool
{
  const auto current = settings_.GetAuthoringTarget();
  std::uint64_t revision = 0;
  constexpr std::string_view prefix = "scene:";
  return current && target.starts_with(prefix)
    && Number(target.substr(prefix.size()), revision)
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

auto PostProcessConsoleBindings::Targets() -> Result
{
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

auto PostProcessConsoleBindings::Inspect(const Args& args) -> Result
{
  const auto& target = args.GetString(kTarget);
  if (!IsCurrentTarget(target)) {
    return StaleTarget();
  }
  const auto settings = settings_.GetExposureSettings();
  settings_.BindVortexRenderer(renderer_());
  auto text = fmt::format(
    "{} epoch={} enabled={} mode={} metering={} mask={}\n", target,
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

auto PostProcessConsoleBindings::Exposure(const Args& args) -> Result
{
  if (!IsCurrentTarget(args.GetString(kTarget))) {
    return StaleTarget();
  }
  auto next = settings_.GetExposureSettings();
  bool changed = false;
  if (args.Has("enabled")) {
    next.enabled = args.GetBool("enabled");
    changed = true;
  }
  if (args.Has("mode")) {
    next.mode = ValueOf(kExposureModes, args.GetString("mode"));
    changed = true;
  }
  if (args.Has("metering")) {
    next.metering_mode = ValueOf(kMeteringModes, args.GetString("metering"));
    changed = true;
  }
  if (args.Has("mask")) {
    if (args.GetString("mask") == "off") {
      next.metering_mask = {};
    } else if (settings_.HasSceneMeteringMask()) {
      next.metering_mask = settings_.GetSceneMeteringMask();
    } else {
      return Invalid("The scene has no metering mask.");
    }
    changed = true;
  }
  for (const auto& [name, member] : fields) {
    if (args.Has(name)) {
      next.*member = static_cast<float>(args.GetDouble(name));
      changed = true;
    }
  }
  if (!changed) {
    return Invalid("Give at least one exposure field.");
  }
  return settings_.TrySetExposureSettings(next)
    ? Accepted()
    : Invalid(std::string(settings_.GetValidationError()));
}

auto PostProcessConsoleBindings::Camera(const Args& args) -> Result
{
  if (!IsCurrentTarget(args.GetString(kTarget))) {
    return StaleTarget();
  }
  const scene::CameraExposure next {
    .aperture_f = static_cast<float>(args.GetDouble("aperture_f")),
    .shutter_rate = static_cast<float>(args.GetDouble("shutter_rate")),
    .iso = static_cast<float>(args.GetDouble("iso")),
  };
  return settings_.TrySetCameraExposure(next)
    ? Accepted()
    : Invalid(std::string(settings_.GetValidationError()));
}

auto PostProcessConsoleBindings::Output(const Args& args) -> Result
{
  if (!IsCurrentTarget(args.GetString(kTarget))) {
    return StaleTarget();
  }
  const auto mode = ValueOf(kToneCurves, args.GetString("tone_curve"));
  return settings_.TrySetOutputSettings(
           mode, static_cast<float>(args.GetDouble("gamma")))
    ? Accepted()
    : Invalid(std::string(settings_.GetValidationError()));
}

auto PostProcessConsoleBindings::Curve(const Args& args) -> Result
{
  if (!IsCurrentTarget(args.GetString(kTarget))) {
    return StaleTarget();
  }
  const auto tokens = console::Parser::Tokenize(args.GetString("keys"));
  std::vector<scene::ExposureCompensationKey> keys;
  if (tokens.size() != 1 || tokens.front() != "clear") {
    if (tokens.size() > engine::kMaxExposureCompensationCurveKeys) {
      return Invalid("At most 64 curve keys are supported.");
    }
    for (const auto& token : tokens) {
      const auto text = std::string_view(token);
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

auto PostProcessConsoleBindings::ResetAuto(const Args& args) -> Result
{
  if (!IsCurrentTarget(args.GetString(kTarget))) {
    return StaleTarget();
  }
  settings_.ResetAutoExposureDefaults();
  return Accepted();
}

auto PostProcessConsoleBindings::Transition(const Args& args) -> Result
{
  const auto renderer = renderer_();
  if (!renderer) {
    return Error("Renderer unavailable.");
  }
  const auto owner = Owner(*renderer, args.GetInt(kOwner));
  if (!owner) {
    return Invalid("Invalid/stale exposure owner. Use pp.targets; borrowers "
                   "are not owners.");
  }
  const auto& policy_name = args.GetString("policy");
  auto policy = vortex::ExposureTransitionPolicy::kRemeter;
  std::optional<float> seed;
  if (policy_name == "seed") {
    if (!args.Has("ev100")) {
      return Invalid("seed needs ev100.");
    }
    seed = static_cast<float>(args.GetDouble("ev100"));
    policy = vortex::ExposureTransitionPolicy::kSeedFromEv100;
  } else if (args.Has("ev100")) {
    return Invalid("ev100 goes only with seed.");
  } else if (policy_name == "preserve") {
    policy = vortex::ExposureTransitionPolicy::kPreserve;
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

auto PostProcessConsoleBindings::TransitionStatus(const Args& args) -> Result
{
  const auto renderer = renderer_();
  if (!renderer) {
    return Error("Renderer unavailable.");
  }
  const auto owner = Owner(*renderer, args.GetInt(kOwner));
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
