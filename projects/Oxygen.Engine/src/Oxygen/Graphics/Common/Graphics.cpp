//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Internal/CommandListPool.h>
#include <Oxygen/Graphics/Common/Internal/Commander.h>
#include <Oxygen/Graphics/Common/Internal/DeferredReclaimerComponent.h>
#include <Oxygen/Graphics/Common/Internal/FramebufferImpl.h>
#include <Oxygen/Graphics/Common/Internal/QueueManager.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>

using oxygen::Graphics;
using oxygen::graphics::internal::Commander;
using oxygen::graphics::internal::CommandListPool;
using oxygen::graphics::internal::DeferredReclaimerComponent;
using oxygen::graphics::internal::QueueManager;

namespace {
constexpr std::string_view kCVarGraphicsVsync = "gfx.vsync";
constexpr std::string_view kCommandGraphicsCaptureStatus = "gfx.capture.status";
constexpr std::string_view kCommandGraphicsCaptureFrame = "gfx.capture.frame";
constexpr std::string_view kCommandGraphicsCaptureBegin = "gfx.capture.begin";
constexpr std::string_view kCommandGraphicsCaptureEnd = "gfx.capture.end";
constexpr std::string_view kCommandGraphicsCaptureDiscard
  = "gfx.capture.discard";
constexpr std::string_view kCommandGraphicsCaptureOpenUi
  = "gfx.capture.open_ui";
constexpr auto kReadbackShutdownTimeout = std::chrono::milliseconds { 3000 };

auto CaptureCommandError(const std::string_view error)
  -> oxygen::console::ExecutionResult
{
  return oxygen::console::ExecutionResult {
    .status = oxygen::console::ExecutionStatus::kError,
    .exit_code = 1,
    .output = {},
    .error = std::string(error),
  };
}

auto CaptureCommandOk(const std::string_view output)
  -> oxygen::console::ExecutionResult
{
  return oxygen::console::ExecutionResult {
    .status = oxygen::console::ExecutionStatus::kOk,
    .exit_code = 0,
    .output = std::string(output),
    .error = {},
  };
}

constexpr std::array kAllCaptureFeatures {
  oxygen::graphics::FrameCaptureFeature::kTriggerNextFrame,
  oxygen::graphics::FrameCaptureFeature::kManualCapture,
  oxygen::graphics::FrameCaptureFeature::kDiscardCapture,
  oxygen::graphics::FrameCaptureFeature::kCaptureFileTemplate,
  oxygen::graphics::FrameCaptureFeature::kReplayUI,
};

auto JoinCaptureFeatureNames(
  const oxygen::observer_ptr<oxygen::graphics::FrameCaptureController>
    frame_capture,
  const bool supported) -> std::string
{
  std::string out;
  for (const auto feature : kAllCaptureFeatures) {
    if (frame_capture->SupportsFeature(feature) != supported) {
      continue;
    }
    if (!out.empty()) {
      out += ", ";
    }
    out += oxygen::graphics::to_string(feature);
  }
  if (out.empty()) {
    return "none";
  }
  return out;
}

auto JoinCaptureCommandNames(const std::vector<std::string_view>& commands)
  -> std::string
{
  std::string out;
  for (const auto command : commands) {
    if (!out.empty()) {
      out += ", ";
    }
    out += command;
  }
  return out;
}

auto BuildCaptureStatusOutput(
  const oxygen::observer_ptr<oxygen::graphics::FrameCaptureController>
    frame_capture) -> std::string
{
  std::vector<std::string_view> meaningful_commands {
    kCommandGraphicsCaptureStatus,
  };
  if (frame_capture->SupportsFeature(
        oxygen::graphics::FrameCaptureFeature::kTriggerNextFrame)) {
    meaningful_commands.push_back(kCommandGraphicsCaptureFrame);
  }
  if (frame_capture->SupportsFeature(
        oxygen::graphics::FrameCaptureFeature::kManualCapture)) {
    meaningful_commands.push_back(kCommandGraphicsCaptureBegin);
    meaningful_commands.push_back(kCommandGraphicsCaptureEnd);
  }
  if (frame_capture->SupportsFeature(
        oxygen::graphics::FrameCaptureFeature::kDiscardCapture)) {
    meaningful_commands.push_back(kCommandGraphicsCaptureDiscard);
  }
  if (frame_capture->SupportsFeature(
        oxygen::graphics::FrameCaptureFeature::kReplayUI)) {
    meaningful_commands.push_back(kCommandGraphicsCaptureOpenUi);
  }

  std::string output;
  output += "provider: " + std::string(frame_capture->GetProviderName());
  output += "\navailable: "
    + std::string(frame_capture->IsAvailable() ? "yes" : "no");
  output += "\ncapturing: "
    + std::string(frame_capture->IsCapturing() ? "yes" : "no");
  output
    += "\nsupported features: " + JoinCaptureFeatureNames(frame_capture, true);
  output += "\nunsupported features: "
    + JoinCaptureFeatureNames(frame_capture, false);
  output
    += "\nmeaningful commands: " + JoinCaptureCommandNames(meaningful_commands);
  if (frame_capture->SupportsFeature(
        oxygen::graphics::FrameCaptureFeature::kCaptureFileTemplate)) {
    output
      += "\ncapture file template: use frame_capture.capture_file_template "
         "or --capture-output";
  }
  if (!frame_capture->IsAvailable()) {
    output += "\navailability note: provider is configured but not currently "
              "ready; inspect the raw state below for the blocking reason";
  }
  output += "\nraw state:\n";
  output += frame_capture->DescribeState();
  return output;
}

template <typename TAction>
auto ExecuteCaptureCommand(
  const oxygen::observer_ptr<oxygen::graphics::FrameCaptureController>
    frame_capture,
  const std::string_view failure_message,
  const std::string_view success_message, TAction&& action)
  -> oxygen::console::ExecutionResult
{
  if (frame_capture == nullptr) {
    return CaptureCommandError("frame capture provider not configured");
  }

  try {
    if (!std::forward<TAction>(action)()) {
      return CaptureCommandError(failure_message);
    }
  } catch (const std::runtime_error& ex) {
    return CaptureCommandError(ex.what());
  }

  return CaptureCommandOk(success_message);
}

class ResourceRegistryComponent : public oxygen::Component {
  OXYGEN_COMPONENT(ResourceRegistryComponent)

public:
  explicit ResourceRegistryComponent(std::string_view name)
    : registry_(std::make_unique<oxygen::graphics::ResourceRegistry>(name))
  {
  }

  [[nodiscard]] auto GetRegistry() const -> const auto& { return *registry_; }

  OXYGEN_MAKE_NON_COPYABLE(ResourceRegistryComponent)
  OXYGEN_DEFAULT_MOVABLE(ResourceRegistryComponent)

  ~ResourceRegistryComponent() override = default;

private:
  std::unique_ptr<oxygen::graphics::ResourceRegistry> registry_ {};
};

} // namespace

Graphics::Graphics(const std::string_view name)
{
  AddComponent<ObjectMetadata>(name);
  AddComponent<ResourceRegistryComponent>(name);
  AddComponent<QueueManager>();
  // CommandListPool before DeferredReclaimer
  AddComponent<CommandListPool>(
    [this](graphics::QueueRole role,
      std::string_view name) -> std::unique_ptr<graphics::CommandList> {
      return this->CreateCommandListImpl(role, name);
    });
  // DeferredReclaimer must be created before Commander because Commander
  // depends on oxygen::graphics::detail::DeferredReclaimer.
  AddComponent<DeferredReclaimerComponent>();
  AddComponent<Commander>();
}

Graphics::~Graphics()
{
  // Clear the CommandList pool
  auto& command_list_pool = GetComponent<CommandListPool>();
  command_list_pool.Clear();
}

auto Graphics::ActivateAsync(co::TaskStarted<> started) -> co::Co<>
{
  DLOG_F(INFO, "Graphics Live Object activating...");
  return OpenNursery(nursery_, std::move(started));
}

auto Graphics::Run() -> void
{
  DLOG_F(INFO, "Starting Graphics backend async tasks...");
}

auto Graphics::IsRunning() const -> bool { return nursery_ != nullptr; }

auto Graphics::Flush() -> void
{
  DLOG_SCOPE_FUNCTION(1);
  // Flush all command queues
  FlushCommandQueues();

  // Process All deferred releases
  auto& reclaimer = GetComponent<DeferredReclaimerComponent>();
  reclaimer.ProcessAllDeferredReleases();
}

auto Graphics::Stop() -> void
{
  LOG_SCOPE_FUNCTION(INFO);

  if (const auto readback_manager = GetReadbackManager();
    readback_manager != nullptr) {
    if (const auto shutdown_result
      = readback_manager->Shutdown(kReadbackShutdownTimeout);
      !shutdown_result.has_value()) {
      LOG_F(WARNING, "Readback drain failed: {}",
        make_error_code(shutdown_result.error()).message());
    }
  }

  if (!IsRunning()) {
    return;
  }

  nursery_->Cancel();
  DLOG_F(INFO, "Graphics Live Object stopped");
}

auto Graphics::BeginFrame(const frame::SequenceNumber frame_number,
  const frame::Slot frame_slot) -> void
{
  CHECK_LT_F(frame_slot, frame::kMaxSlot, "Frame slot out of bounds");

  // Flush all command queues to ensure GPU work is submitted before releasing
  // resources
  FlushCommandQueues();

  if (const auto readback_manager = GetReadbackManager();
    readback_manager != nullptr) {
    readback_manager->OnFrameStart(frame_slot);
  }

  auto& reclaimer = GetComponent<DeferredReclaimerComponent>();
  reclaimer.OnBeginFrame(frame_slot);

  if (const auto frame_capture = GetFrameCaptureController();
    frame_capture != nullptr) {
    frame_capture->OnBeginFrame(frame_number, frame_slot);
  }
}

auto Graphics::EndFrame(const frame::SequenceNumber frame_number,
  const frame::Slot frame_slot) -> void
{
  if (const auto frame_capture = GetFrameCaptureController();
    frame_capture != nullptr) {
    frame_capture->OnEndFrame(frame_number, frame_slot);
  }
}

auto Graphics::PresentSurfaces(
  const std::vector<observer_ptr<graphics::Surface>>& surfaces) -> void
{
  DLOG_SCOPE_FUNCTION(1);

  const auto frame_capture = GetFrameCaptureController();
  for (const auto& surface : surfaces) {
    try {
      if (frame_capture != nullptr) {
        frame_capture->OnPresentSurface(surface);
      }
      surface->Present();
    } catch (const std::exception& e) {
      LOG_F(WARNING, "Present on surface `{}` failed; frame discarded: {}",
        surface->GetName(), e.what());
    }
  }
}

auto Graphics::SetVSyncEnabled([[maybe_unused]] const bool enabled) -> void { }

auto Graphics::RegisterConsoleBindings(
  const observer_ptr<console::Console> console) noexcept -> void
{
  if (console == nullptr) {
    return;
  }

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarGraphicsVsync),
    .help = "Enable graphics VSync",
    .default_value = true,
    .flags = console::CVarFlags::kArchive,
  });

  (void)console->RegisterCommand(console::CommandDefinition {
    .name = std::string(kCommandGraphicsCaptureStatus),
    .help = "Describe frame capture integration state",
    .flags = console::CommandFlags::kDevOnly,
    .handler = [this](const std::vector<std::string>& args,
                 const console::CommandContext&) -> console::ExecutionResult {
      if (!args.empty()) {
        return console::ExecutionResult {
          .status = console::ExecutionStatus::kInvalidArguments,
          .exit_code = 2,
          .output = {},
          .error = "usage: gfx.capture.status",
        };
      }

      const auto frame_capture = GetFrameCaptureController();
      if (frame_capture == nullptr) {
        return console::ExecutionResult {
          .status = console::ExecutionStatus::kOk,
          .exit_code = 0,
          .output = "frame capture provider not configured",
          .error = {},
        };
      }

      return console::ExecutionResult {
        .status = console::ExecutionStatus::kOk,
        .exit_code = 0,
        .output = BuildCaptureStatusOutput(frame_capture),
        .error = {},
      };
    },
  });

  (void)console->RegisterCommand(console::CommandDefinition {
    .name = std::string(kCommandGraphicsCaptureFrame),
    .help = "Capture the next rendered frame",
    .flags = console::CommandFlags::kDevOnly,
    .handler = [this](const std::vector<std::string>& args,
                 const console::CommandContext&) -> console::ExecutionResult {
      if (!args.empty()) {
        return console::ExecutionResult {
          .status = console::ExecutionStatus::kInvalidArguments,
          .exit_code = 2,
          .output = {},
          .error = "usage: gfx.capture.frame",
        };
      }

      const auto frame_capture = GetFrameCaptureController();
      return ExecuteCaptureCommand(frame_capture,
        "failed to arm next-frame capture", "next-frame capture armed",
        [frame_capture] { return frame_capture->TriggerNextFrame(); });
    },
  });

  (void)console->RegisterCommand(console::CommandDefinition {
    .name = std::string(kCommandGraphicsCaptureBegin),
    .help = "Begin a manual frame capture",
    .flags = console::CommandFlags::kDevOnly,
    .handler = [this](const std::vector<std::string>& args,
                 const console::CommandContext&) -> console::ExecutionResult {
      if (!args.empty()) {
        return console::ExecutionResult {
          .status = console::ExecutionStatus::kInvalidArguments,
          .exit_code = 2,
          .output = {},
          .error = "usage: gfx.capture.begin",
        };
      }

      const auto frame_capture = GetFrameCaptureController();
      return ExecuteCaptureCommand(frame_capture,
        "failed to begin frame capture", "frame capture started",
        [frame_capture] { return frame_capture->StartCapture(); });
    },
  });

  (void)console->RegisterCommand(console::CommandDefinition {
    .name = std::string(kCommandGraphicsCaptureEnd),
    .help = "End a manual frame capture",
    .flags = console::CommandFlags::kDevOnly,
    .handler = [this](const std::vector<std::string>& args,
                 const console::CommandContext&) -> console::ExecutionResult {
      if (!args.empty()) {
        return console::ExecutionResult {
          .status = console::ExecutionStatus::kInvalidArguments,
          .exit_code = 2,
          .output = {},
          .error = "usage: gfx.capture.end",
        };
      }

      const auto frame_capture = GetFrameCaptureController();
      return ExecuteCaptureCommand(frame_capture, "failed to end frame capture",
        "frame capture ended",
        [frame_capture] { return frame_capture->EndCapture(); });
    },
  });

  (void)console->RegisterCommand(console::CommandDefinition {
    .name = std::string(kCommandGraphicsCaptureDiscard),
    .help = "Discard an active frame capture",
    .flags = console::CommandFlags::kDevOnly,
    .handler = [this](const std::vector<std::string>& args,
                 const console::CommandContext&) -> console::ExecutionResult {
      if (!args.empty()) {
        return console::ExecutionResult {
          .status = console::ExecutionStatus::kInvalidArguments,
          .exit_code = 2,
          .output = {},
          .error = "usage: gfx.capture.discard",
        };
      }

      const auto frame_capture = GetFrameCaptureController();
      return ExecuteCaptureCommand(frame_capture,
        "failed to discard frame capture", "frame capture discarded",
        [frame_capture] { return frame_capture->DiscardCapture(); });
    },
  });

  (void)console->RegisterCommand(console::CommandDefinition {
    .name = std::string(kCommandGraphicsCaptureOpenUi),
    .help = "Launch the capture provider replay UI",
    .flags = console::CommandFlags::kDevOnly,
    .handler = [this](const std::vector<std::string>& args,
                 const console::CommandContext&) -> console::ExecutionResult {
      if (!args.empty()) {
        return console::ExecutionResult {
          .status = console::ExecutionStatus::kInvalidArguments,
          .exit_code = 2,
          .output = {},
          .error = "usage: gfx.capture.open_ui",
        };
      }

      const auto frame_capture = GetFrameCaptureController();
      return ExecuteCaptureCommand(frame_capture, "failed to launch replay UI",
        "replay UI launch requested",
        [frame_capture] { return frame_capture->LaunchReplayUI(); });
    },
  });
}

auto Graphics::ApplyConsoleCVars(const console::Console& console) -> void
{
  bool vsync_enabled = true;
  if (console.TryGetCVarValue<bool>(kCVarGraphicsVsync, vsync_enabled)) {
    SetVSyncEnabled(vsync_enabled);
  }
}

auto Graphics::CreateCommandQueues(
  const graphics::QueuesStrategy& queue_strategy) -> void
{
  // Delegate queue management to the installed QueueManager component which
  // will call back to this backend's CreateCommandQueue hook when it needs to
  // instantiate actual CommandQueue objects.
  auto& qm = GetComponent<QueueManager>();
  qm.CreateQueues(queue_strategy,
    [this](const graphics::QueueKey& key, const graphics::QueueRole role)
      -> std::shared_ptr<graphics::CommandQueue> {
      return this->CreateCommandQueue(key, role);
    });
}

auto Graphics::QueueKeyFor(graphics::QueueRole role) const -> graphics::QueueKey
{
  auto& qm = GetComponent<QueueManager>();
  return qm.QueueKeyFor(role);
}

auto Graphics::FlushCommandQueues() -> void
{
  // Forward to the QueueManager which enumerates unique queues safely.
  auto& qm = GetComponent<QueueManager>();
  qm.ForEachQueue([](const graphics::CommandQueue& q) { q.Flush(); });
}

auto Graphics::GetCommandQueue(const graphics::QueueKey& key) const
  -> observer_ptr<graphics::CommandQueue>
{
  auto& qm = GetComponent<QueueManager>();
  return qm.GetQueueByName(key);
}

auto Graphics::GetCommandQueue(const graphics::QueueRole role) const
  -> observer_ptr<graphics::CommandQueue>
{
  auto& qm = GetComponent<QueueManager>();
  return qm.GetQueueByRole(role);
}

auto Graphics::AcquireCommandRecorder(const graphics::QueueKey& queue_key,
  const std::string_view command_list_name, const bool immediate_submission)
  -> std::unique_ptr<graphics::CommandRecorder,
    std::function<void(graphics::CommandRecorder*)>>
{
  // Get the command queue from the queue key
  auto queue = GetCommandQueue(queue_key);
  DCHECK_NOTNULL_F(
    queue, "Failed to get command queue for key '{}'", queue_key.get());

  // Acquire a command list
  auto command_list
    = AcquireCommandList(queue->GetQueueRole(), command_list_name);
  DCHECK_NOTNULL_F(command_list, "Failed to acquire command list");

  // Create backend recorder and forward to the Commander component which will
  // wrap it with the appropriate deleter behavior.
  auto recorder = CreateCommandRecorder(command_list, queue);
  auto& cmdr = GetComponent<Commander>();
  return cmdr.PrepareCommandRecorder(
    std::move(recorder), std::move(command_list), immediate_submission);
}

auto Graphics::SubmitDeferredCommandLists() -> void
{
  GetComponent<Commander>().SubmitDeferredCommandLists();
}

auto Graphics::AcquireCommandList(
  graphics::QueueRole queue_role, const std::string_view command_list_name)
  -> std::shared_ptr<graphics::CommandList>
{
  auto& command_list_pool = GetComponent<CommandListPool>();
  return command_list_pool.AcquireCommandList(queue_role, command_list_name);
}

auto Graphics::GetDescriptorAllocator() -> graphics::DescriptorAllocator&
{
  return const_cast<graphics::DescriptorAllocator&>(
    std::as_const(*this).GetDescriptorAllocator());
}

auto Graphics::GetResourceRegistry() const -> const graphics::ResourceRegistry&
{
  return GetComponent<ResourceRegistryComponent>().GetRegistry();
}

auto Graphics::GetResourceRegistry() -> graphics::ResourceRegistry&
{
  return const_cast<graphics::ResourceRegistry&>(
    std::as_const(*this).GetResourceRegistry());
}
auto Graphics::GetDeferredReclaimer() -> graphics::detail::DeferredReclaimer&
{
  // The actual component stored in the composition is the internal
  // DeferredReclaimerComponent. Return a reference to the public
  // DeferredReclaimer interface implemented by that component.
  auto& comp = GetComponent<graphics::internal::DeferredReclaimerComponent>();
  return static_cast<graphics::detail::DeferredReclaimer&>(comp);
}

auto Graphics::GetReadbackManager() const
  -> observer_ptr<graphics::ReadbackManager>
{
  return {};
}

auto Graphics::RegisterDeferredRelease(
  std::shared_ptr<graphics::Surface> surface) -> void
{
  // Forward surface ownership to the per-frame DeferredReclaimer which will
  // ensure final release happens on the engine's frame timeline and not on
  // caller threads (e.g. UI/interop thread).
  if (!surface) {
    return;
  }

  auto& reclaimer
    = GetComponent<graphics::internal::DeferredReclaimerComponent>();
  reclaimer.RegisterDeferredRelease(std::move(surface));
}

auto Graphics::GetTimestampQueryProvider() const
  -> observer_ptr<graphics::TimestampQueryProvider>
{
  return {};
}

auto Graphics::GetFrameCaptureController() const
  -> observer_ptr<graphics::FrameCaptureController>
{
  return {};
}

auto Graphics::CreateFramebuffer(const graphics::FramebufferDesc& desc)
  -> std::shared_ptr<graphics::Framebuffer>
{
  return std::make_shared<graphics::internal::FramebufferImpl>(
    desc, weak_from_this());
}
