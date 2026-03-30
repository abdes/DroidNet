//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <windows.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/RenderDocFrameCaptureController.h>

#if defined(USE_RENDERDOC) && __has_include(<renderdoc_app.h>)
#  include <renderdoc_app.h>
#endif

namespace {

auto InitModeText(const oxygen::FrameCaptureInitMode init_mode)
  -> std::string_view
{
  switch (init_mode) {
  case oxygen::FrameCaptureInitMode::kDisabled:
    return "disabled";
  case oxygen::FrameCaptureInitMode::kAttachedOnly:
    return "attached";
  case oxygen::FrameCaptureInitMode::kSearchPath:
    return "search";
  case oxygen::FrameCaptureInitMode::kExplicitPath:
    return "path";
  }
  return "disabled";
}

#if defined(USE_RENDERDOC) && __has_include(<renderdoc_app.h>)

constexpr wchar_t kRenderDocModuleName[] = L"renderdoc.dll";

enum class CaptureMode : uint8_t { kNone, kManual };

constexpr auto kRenderDocSupportedFeatures
  = oxygen::graphics::FrameCaptureFeature::kTriggerNextFrame
  | oxygen::graphics::FrameCaptureFeature::kManualCapture
  | oxygen::graphics::FrameCaptureFeature::kDiscardCapture
  | oxygen::graphics::FrameCaptureFeature::kCaptureFileTemplate
  | oxygen::graphics::FrameCaptureFeature::kReplayUI;

struct RenderDocCaptureTarget {
  RENDERDOC_DevicePointer device { nullptr };
  RENDERDOC_WindowHandle window { nullptr };
};

auto ToWidePath(const std::string_view path) -> std::wstring
{
  return std::filesystem::path(std::string(path)).wstring();
}

auto NarrowPath(const std::filesystem::path& path) -> std::string
{
  return path.generic_string();
}

auto LastErrorMessage(const std::string_view action) -> std::string
{
  return std::string(action) + " failed with Win32 error "
    + std::to_string(::GetLastError());
}

auto ToEngineFrameSequence(const uint64_t zero_based_frame_index) -> uint64_t
{
  return zero_based_frame_index + 1U;
}

auto HasExplicitTarget(const RenderDocCaptureTarget& target) -> bool
{
  return target.device != nullptr && target.window != nullptr;
}

class RenderDocFrameCaptureController final
  : public oxygen::graphics::FrameCaptureController {
public:
  RenderDocFrameCaptureController(oxygen::graphics::d3d12::Graphics& graphics,
    oxygen::FrameCaptureConfig config)
    : graphics_(graphics)
    , config_(std::move(config))
  {
    Initialize();
  }

  ~RenderDocFrameCaptureController() override
  {
    if (owns_module_ && module_handle_ != nullptr) {
      ::FreeLibrary(module_handle_);
      module_handle_ = nullptr;
    }
  }

  [[nodiscard]] auto GetProviderName() const noexcept
    -> std::string_view override
  {
    return "RenderDoc";
  }

  [[nodiscard]] auto IsAvailable() const noexcept -> bool override
  {
    return api_ != nullptr;
  }

  [[nodiscard]] auto IsCapturing() const noexcept -> bool override
  {
    std::scoped_lock lock(mutex_);
    return IsCapturingUnlocked();
  }

  [[nodiscard]] auto GetSupportedFeatures() const noexcept
    -> oxygen::graphics::FrameCaptureFeature override
  {
    return kRenderDocSupportedFeatures;
  }

  [[nodiscard]] auto DescribeState() const -> std::string override
  {
    std::scoped_lock lock(mutex_);

    std::ostringstream out;
    out << "provider=RenderDoc"
        << " available=" << (api_ != nullptr ? "true" : "false")
        << " capturing=" << (IsCapturingUnlocked() ? "true" : "false")
        << " capture_from_frame=" << scheduled_capture_from_frame_
        << " capture_frame_count=" << scheduled_capture_frame_count_
        << " active_target_window="
        << (last_present_target_.window != nullptr ? "true" : "false")
        << " features=" << DescribeSupportedFeatures()
        << " init_mode=" << InitModeText(config_.init_mode);

    if (!resolved_module_path_.empty()) {
      out << " module=" << resolved_module_path_;
    }
    if (api_ != nullptr) {
      out << " api=" << api_major_ << '.' << api_minor_ << '.' << api_patch_;
    }
    if (!status_message_.empty()) {
      out << " status=\"" << status_message_ << '"';
    }

    return out.str();
  }

private:
  auto DoTriggerNextFrame() -> bool override
  {
    std::scoped_lock lock(mutex_);

    if (api_ == nullptr) {
      status_message_ = "RenderDoc API unavailable";
      LOG_F(
        WARNING, "RenderDoc next-frame capture rejected: {}", status_message_);
      return false;
    }
    if (IsCapturingUnlocked()) {
      status_message_ = "capture already in progress";
      LOG_F(
        WARNING, "RenderDoc next-frame capture rejected: {}", status_message_);
      return false;
    }
    if (api_->TriggerCapture == nullptr) {
      status_message_ = "RenderDoc trigger capture is unavailable";
      LOG_F(
        WARNING, "RenderDoc next-frame capture rejected: {}", status_message_);
      return false;
    }

    if (!SetActiveCaptureTargetUnlocked()) {
      LOG_F(WARNING,
        "RenderDoc next-frame capture has no observed present target; using "
        "provider active window");
    }
    api_->TriggerCapture();
    status_message_ = "next-frame capture requested";
    LOG_F(INFO, "RenderDoc next-frame capture requested");
    return true;
  }

  auto DoStartCapture(oxygen::observer_ptr<oxygen::graphics::Surface> surface)
    -> bool override
  {
    std::scoped_lock lock(mutex_);

    if (api_ == nullptr || IsCapturingUnlocked()) {
      status_message_ = api_ == nullptr ? "RenderDoc API unavailable"
                                        : "capture already in progress";
      LOG_F(WARNING, "RenderDoc manual capture rejected: {}", status_message_);
      return false;
    }

    if (!StartCaptureUnlocked(surface)) {
      LOG_F(WARNING, "RenderDoc manual capture failed: {}", status_message_);
      return false;
    }

    capture_mode_ = CaptureMode::kManual;
    status_message_ = "manual capture started";
    LOG_F(INFO, "RenderDoc manual capture started");
    return true;
  }

  auto DoEndCapture(oxygen::observer_ptr<oxygen::graphics::Surface> surface)
    -> bool override
  {
    std::scoped_lock lock(mutex_);

    if (api_ == nullptr || !IsCapturingUnlocked()) {
      status_message_ = api_ == nullptr ? "RenderDoc API unavailable"
                                        : "no active capture to end";
      LOG_F(WARNING, "RenderDoc capture end rejected: {}", status_message_);
      return false;
    }

    const bool ended = EndCaptureUnlocked(surface);
    if (ended) {
      capture_mode_ = CaptureMode::kNone;
      status_message_ = "capture ended";
      LOG_F(INFO, "RenderDoc capture ended");
    } else {
      LOG_F(WARNING, "RenderDoc capture end failed: {}", status_message_);
    }
    return ended;
  }

  auto DoDiscardCapture(oxygen::observer_ptr<oxygen::graphics::Surface> surface)
    -> bool override
  {
    std::scoped_lock lock(mutex_);

    if (api_ == nullptr) {
      status_message_ = "RenderDoc API unavailable";
      LOG_F(WARNING, "RenderDoc capture discard rejected: {}", status_message_);
      return false;
    }

    if (scheduled_capture_frame_count_ > 0 && !IsCapturingUnlocked()) {
      scheduled_capture_from_frame_ = 0;
      scheduled_capture_frame_count_ = 0;
      capture_mode_ = CaptureMode::kNone;
      status_message_ = "configured frame-range capture request cleared";
      LOG_F(INFO, "RenderDoc configured frame-range capture request cleared");
      return true;
    }

    if (!IsCapturingUnlocked()) {
      status_message_ = "no active capture to discard";
      LOG_F(WARNING, "RenderDoc capture discard rejected: {}", status_message_);
      return false;
    }

    const bool discarded = DiscardCaptureUnlocked(surface);
    if (discarded) {
      capture_mode_ = CaptureMode::kNone;
      status_message_ = "capture discarded";
      LOG_F(INFO, "RenderDoc capture discarded");
    } else {
      LOG_F(WARNING, "RenderDoc capture discard failed: {}", status_message_);
    }
    return discarded;
  }

  auto DoSetCaptureFileTemplate(const std::string_view path_template)
    -> bool override
  {
    std::scoped_lock lock(mutex_);

    config_.capture_file_template = std::string(path_template);
    if (api_ == nullptr || api_->SetCaptureFilePathTemplate == nullptr) {
      status_message_ = "RenderDoc API unavailable";
      LOG_F(WARNING, "RenderDoc capture file template update rejected: {}",
        status_message_);
      return false;
    }

    api_->SetCaptureFilePathTemplate(config_.capture_file_template.c_str());
    status_message_ = "capture file template updated";
    LOG_F(INFO, "RenderDoc capture file template updated: {}",
      config_.capture_file_template);
    return true;
  }

  auto DoLaunchReplayUI(const bool connect_target_control) -> bool override
  {
    std::scoped_lock lock(mutex_);

    if (api_ == nullptr || api_->LaunchReplayUI == nullptr) {
      status_message_ = "RenderDoc replay UI unavailable";
      LOG_F(
        WARNING, "RenderDoc replay UI launch rejected: {}", status_message_);
      return false;
    }

    if (api_->LaunchReplayUI(connect_target_control ? 1U : 0U, nullptr) == 0U) {
      status_message_ = "failed to launch RenderDoc replay UI";
      LOG_F(WARNING, "RenderDoc replay UI launch failed: {}", status_message_);
      return false;
    }

    status_message_ = "RenderDoc replay UI launch requested";
    LOG_F(INFO, "RenderDoc replay UI launch requested");
    return true;
  }

  auto OnBeginFrame(oxygen::frame::SequenceNumber frame_number,
    oxygen::frame::Slot /*frame_slot*/) -> void override
  {
    std::scoped_lock lock(mutex_);

    if (api_ == nullptr || IsCapturingUnlocked()) {
      return;
    }

    if (scheduled_capture_frame_count_ == 0
      || frame_number.get()
        != ToEngineFrameSequence(scheduled_capture_from_frame_)) {
      return;
    }

    const auto requested_from_frame = scheduled_capture_from_frame_;
    const auto requested_frame_count = scheduled_capture_frame_count_;
    scheduled_capture_from_frame_ = 0;
    scheduled_capture_frame_count_ = 0;

    if (requested_frame_count == 1) {
      if (api_->TriggerCapture == nullptr) {
        status_message_ = "RenderDoc trigger capture is unavailable";
        LOG_F(WARNING, "RenderDoc configured frame capture rejected: {}",
          status_message_);
        return;
      }
      if (!SetActiveCaptureTargetUnlocked()) {
        LOG_F(WARNING,
          "RenderDoc configured frame capture for frame {} has no observed "
          "present target; using provider active window",
          requested_from_frame);
      }
      api_->TriggerCapture();
      status_message_ = "configured frame capture requested for frame "
        + std::to_string(requested_from_frame);
      LOG_F(INFO, "RenderDoc configured frame capture requested for frame {}",
        requested_from_frame);
      return;
    }

    if (api_->TriggerMultiFrameCapture == nullptr) {
      status_message_ = "RenderDoc multi-frame capture is unavailable";
      LOG_F(WARNING, "RenderDoc configured frame-range capture rejected: {}",
        status_message_);
      return;
    }

    if (!SetActiveCaptureTargetUnlocked()) {
      LOG_F(WARNING,
        "RenderDoc configured frame-range capture from frame {} for {} "
        "frame(s) has no observed present target; using provider active window",
        requested_from_frame, requested_frame_count);
    }
    api_->TriggerMultiFrameCapture(requested_frame_count);
    status_message_ = "configured frame-range capture requested from frame "
      + std::to_string(requested_from_frame) + " for "
      + std::to_string(requested_frame_count) + " frame(s)";
    LOG_F(INFO,
      "RenderDoc configured frame-range capture requested from frame {} for {} "
      "frame(s)",
      requested_from_frame, requested_frame_count);
  }

  auto OnEndFrame(oxygen::frame::SequenceNumber frame_number,
    oxygen::frame::Slot /*frame_slot*/) -> void override
  {
    std::scoped_lock lock(mutex_);

    if (api_ == nullptr || capture_mode_ != CaptureMode::kManual) {
      return;
    }

    static_cast<void>(frame_number);
  }

  auto OnPresentSurface(
    const oxygen::observer_ptr<oxygen::graphics::Surface> surface)
    -> void override
  {
    std::scoped_lock lock(mutex_);
    DCHECK_NOTNULL_F(surface, "Presented surface cannot be null");
    if (surface == nullptr) {
      return;
    }

    last_present_target_ = ResolveCaptureTarget(surface);
  }

  auto Initialize() -> void
  {
    if (config_.init_mode == oxygen::FrameCaptureInitMode::kDisabled) {
      status_message_ = "initialization disabled";
      LOG_F(INFO, "RenderDoc frame capture initialization disabled");
      return;
    }

    if (!TryInitializeFromConfig()) {
      LOG_F(WARNING, "RenderDoc frame capture initialization failed: {}",
        status_message_);
      return;
    }

    if (api_ != nullptr && !config_.capture_file_template.empty()) {
      api_->SetCaptureFilePathTemplate(config_.capture_file_template.c_str());
      LOG_F(INFO, "RenderDoc capture file template configured: {}",
        config_.capture_file_template);
    }
    if (api_ != nullptr && config_.frame_count > 0) {
      scheduled_capture_from_frame_ = config_.from_frame;
      scheduled_capture_frame_count_ = config_.frame_count;
      status_message_ = "configured frame-range capture armed from frame "
        + std::to_string(config_.from_frame) + " for "
        + std::to_string(config_.frame_count) + " frame(s)";
      LOG_F(INFO,
        "RenderDoc configured frame-range capture armed from frame {} for {} "
        "frame(s)",
        config_.from_frame, config_.frame_count);
    }
  }

  auto TryInitializeFromConfig() -> bool
  {
    switch (config_.init_mode) {
    case oxygen::FrameCaptureInitMode::kDisabled:
      status_message_ = "initialization disabled";
      return false;
    case oxygen::FrameCaptureInitMode::kAttachedOnly: {
      if (const auto module = ::GetModuleHandleW(kRenderDocModuleName);
        module != nullptr) {
        return BindApi(module, false, "attached RenderDoc module");
      }
      status_message_ = "RenderDoc is not attached to this process";
      return false;
    }
    case oxygen::FrameCaptureInitMode::kSearchPath: {
      if (const auto attached = ::GetModuleHandleW(kRenderDocModuleName);
        attached != nullptr) {
        return BindApi(attached, false, "attached RenderDoc module");
      }

      const auto module = ::LoadLibraryW(kRenderDocModuleName);
      if (module == nullptr) {
        status_message_ = LastErrorMessage("LoadLibraryW(renderdoc.dll)");
        return false;
      }
      return BindApi(module, true, "renderdoc.dll loaded from search path");
    }
    case oxygen::FrameCaptureInitMode::kExplicitPath: {
      if (config_.module_path.empty()) {
        status_message_ = "explicit RenderDoc module path not provided";
        return false;
      }

      const auto module
        = ::LoadLibraryW(ToWidePath(config_.module_path).c_str());
      if (module == nullptr) {
        status_message_
          = LastErrorMessage("LoadLibraryW(explicit RenderDoc module path)");
        return false;
      }
      return BindApi(
        module, true, "RenderDoc module loaded from explicit path");
    }
    }

    status_message_ = "unsupported RenderDoc initialization mode";
    return false;
  }

  auto BindApi(HMODULE module, const bool owns_module, std::string_view source)
    -> bool
  {
    auto* const get_api = reinterpret_cast<pRENDERDOC_GetAPI>(
      ::GetProcAddress(module, "RENDERDOC_GetAPI"));
    if (get_api == nullptr) {
      status_message_
        = std::string(source) + " does not export RENDERDOC_GetAPI";
      if (owns_module) {
        ::FreeLibrary(module);
      }
      return false;
    }

    auto* api = static_cast<RENDERDOC_API_1_6_0*>(nullptr);
    if (get_api(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void**>(&api))
        == 0
      || api == nullptr) {
      status_message_
        = std::string(source) + " does not expose RenderDoc API 1.6.0+";
      if (owns_module) {
        ::FreeLibrary(module);
      }
      return false;
    }

    api_ = api;
    module_handle_ = module;
    owns_module_ = owns_module;
    status_message_ = std::string(source) + " ready";
    resolved_module_path_ = ResolveModulePath(module);
    api_->GetAPIVersion(&api_major_, &api_minor_, &api_patch_);

    LOG_F(INFO,
      "RenderDoc frame capture controller ready: module='{}' api={}.{}.{}",
      resolved_module_path_, api_major_, api_minor_, api_patch_);
    return true;
  }

  [[nodiscard]] auto ResolveCaptureTarget(
    const oxygen::observer_ptr<oxygen::graphics::Surface> surface) const
    -> RenderDocCaptureTarget
  {
    RenderDocCaptureTarget target {};
    auto* const current_device = graphics_.GetCurrentDevice();
    DCHECK_NOTNULL_F(current_device,
      "RenderDoc capture target resolution requires an active graphics device");
    target.device = static_cast<RENDERDOC_DevicePointer>(current_device);

    if (surface == nullptr) {
      return target;
    }

    if (!surface->HasComponent<oxygen::graphics::detail::WindowComponent>()) {
      return target;
    }

    const auto handles
      = surface->GetComponent<oxygen::graphics::detail::WindowComponent>()
          .Native();
    target.window = static_cast<RENDERDOC_WindowHandle>(handles.window_handle);
    return target;
  }

  [[nodiscard]] auto IsCapturingUnlocked() const -> bool
  {
    return api_ != nullptr && api_->IsFrameCapturing != nullptr
      && api_->IsFrameCapturing() != 0U;
  }

  auto StartCaptureUnlocked(
    const oxygen::observer_ptr<oxygen::graphics::Surface> surface) -> bool
  {
    if (api_ == nullptr || api_->StartFrameCapture == nullptr) {
      status_message_ = "RenderDoc capture start is unavailable";
      return false;
    }

    const auto target = ResolveCaptureTarget(surface);
    if (target.device != nullptr && target.window != nullptr
      && api_->SetActiveWindow != nullptr) {
      api_->SetActiveWindow(target.device, target.window);
    }

    api_->StartFrameCapture(target.device, target.window);
    if (!IsCapturingUnlocked()) {
      status_message_ = "RenderDoc did not begin capturing";
      return false;
    }

    return true;
  }

  auto SetActiveCaptureTargetUnlocked() -> bool
  {
    if (api_ == nullptr || api_->SetActiveWindow == nullptr) {
      return false;
    }
    if (!HasExplicitTarget(last_present_target_)) {
      return false;
    }

    api_->SetActiveWindow(
      last_present_target_.device, last_present_target_.window);
    return true;
  }

  auto EndCaptureUnlocked(
    const oxygen::observer_ptr<oxygen::graphics::Surface> surface) -> bool
  {
    if (api_ == nullptr || api_->EndFrameCapture == nullptr) {
      status_message_ = "RenderDoc capture end is unavailable";
      return false;
    }

    const auto target = ResolveCaptureTarget(surface);
    if (api_->EndFrameCapture(target.device, target.window) == 0U) {
      status_message_ = "RenderDoc failed to end the capture";
      return false;
    }

    return true;
  }

  auto DiscardCaptureUnlocked(
    const oxygen::observer_ptr<oxygen::graphics::Surface> surface) -> bool
  {
    if (api_ == nullptr || api_->DiscardFrameCapture == nullptr) {
      status_message_ = "RenderDoc capture discard is unavailable";
      return false;
    }

    const auto target = ResolveCaptureTarget(surface);
    if (api_->DiscardFrameCapture(target.device, target.window) == 0U) {
      status_message_ = "RenderDoc failed to discard the capture";
      return false;
    }

    return true;
  }

  [[nodiscard]] static auto ResolveModulePath(HMODULE module) -> std::string
  {
    wchar_t buffer[MAX_PATH] {};
    const auto length = ::GetModuleFileNameW(module, buffer, MAX_PATH);
    if (length == 0U) {
      return {};
    }
    return NarrowPath(std::filesystem::path(buffer));
  }

  oxygen::graphics::d3d12::Graphics& graphics_;
  oxygen::FrameCaptureConfig config_;
  RENDERDOC_API_1_6_0* api_ { nullptr };
  HMODULE module_handle_ { nullptr };
  bool owns_module_ { false };
  int api_major_ { 0 };
  int api_minor_ { 0 };
  int api_patch_ { 0 };
  std::string resolved_module_path_ {};
  std::string status_message_ {};
  mutable std::mutex mutex_ {};
  RenderDocCaptureTarget last_present_target_ {};
  uint64_t scheduled_capture_from_frame_ { 0 };
  uint32_t scheduled_capture_frame_count_ { 0 };
  CaptureMode capture_mode_ { CaptureMode::kNone };
};

#else

class RenderDocFrameCaptureController final
  : public oxygen::graphics::FrameCaptureController {
public:
  RenderDocFrameCaptureController(
    oxygen::graphics::d3d12::Graphics& /*graphics*/,
    oxygen::FrameCaptureConfig config)
    : config_(std::move(config))
  {
    status_message_ = "RenderDoc support unavailable in this build";
  }

  [[nodiscard]] auto GetProviderName() const noexcept
    -> std::string_view override
  {
    return "RenderDoc";
  }

  [[nodiscard]] auto IsAvailable() const noexcept -> bool override
  {
    return false;
  }

  [[nodiscard]] auto IsCapturing() const noexcept -> bool override
  {
    return false;
  }

  [[nodiscard]] auto GetSupportedFeatures() const noexcept
    -> oxygen::graphics::FrameCaptureFeature override
  {
    return kRenderDocSupportedFeatures;
  }

  [[nodiscard]] auto DescribeState() const -> std::string override
  {
    std::ostringstream out;
    out << "provider=RenderDoc available=false capturing=false features="
        << DescribeSupportedFeatures()
        << " init_mode=" << InitModeText(config_.init_mode);
    if (!status_message_.empty()) {
      out << " status=\"" << status_message_ << '"';
    }
    return out.str();
  }

private:
  auto DoTriggerNextFrame() -> bool override { return false; }

  auto DoStartCapture(
    oxygen::observer_ptr<oxygen::graphics::Surface> /*surface*/ = {})
    -> bool override
  {
    return false;
  }

  auto DoEndCapture(
    oxygen::observer_ptr<oxygen::graphics::Surface> /*surface*/ = {})
    -> bool override
  {
    return false;
  }

  auto DoDiscardCapture(
    oxygen::observer_ptr<oxygen::graphics::Surface> /*surface*/ = {})
    -> bool override
  {
    return false;
  }

  auto DoSetCaptureFileTemplate(std::string_view path_template) -> bool override
  {
    config_.capture_file_template = std::string(path_template);
    return false;
  }

  auto DoLaunchReplayUI(bool /*connect_target_control*/ = true) -> bool override
  {
    return false;
  }

  auto OnBeginFrame(oxygen::frame::SequenceNumber /*frame_number*/,
    oxygen::frame::Slot /*frame_slot*/) -> void override
  {
  }

  auto OnEndFrame(oxygen::frame::SequenceNumber /*frame_number*/,
    oxygen::frame::Slot /*frame_slot*/) -> void override
  {
  }

  auto OnPresentSurface(
    oxygen::observer_ptr<oxygen::graphics::Surface> /*surface*/)
    -> void override
  {
  }

  oxygen::FrameCaptureConfig config_;
  std::string status_message_ {};
};

#endif

} // namespace

namespace oxygen::graphics::d3d12 {

auto CreateRenderDocFrameCaptureController(
  Graphics& graphics, const FrameCaptureConfig& config)
  -> std::unique_ptr<graphics::FrameCaptureController>
{
  return std::make_unique<RenderDocFrameCaptureController>(graphics, config);
}

} // namespace oxygen::graphics::d3d12
