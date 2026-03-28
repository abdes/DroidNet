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
#include <Oxygen/Graphics/External/RenderDoc/renderdoc_app.h>

namespace {

constexpr wchar_t kRenderDocModuleName[] = L"renderdoc.dll";

enum class CaptureMode : uint8_t { kNone, kNextFrame, kManual };

struct RenderDocCaptureTarget {
  RENDERDOC_DevicePointer device { nullptr };
  RENDERDOC_WindowHandle window { nullptr };
};

auto BoolText(const bool value) -> const char*
{
  return value ? "true" : "false";
}

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

  [[nodiscard]] auto DescribeState() const -> std::string override
  {
    std::scoped_lock lock(mutex_);

    std::ostringstream out;
    out << "provider=RenderDoc"
        << " available=" << BoolText(api_ != nullptr)
        << " capturing=" << BoolText(IsCapturingUnlocked())
        << " next_frame_armed=" << BoolText(next_frame_requested_)
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

  auto TriggerNextFrame() -> bool override
  {
    std::scoped_lock lock(mutex_);

    if (api_ == nullptr) {
      return false;
    }
    if (IsCapturingUnlocked()) {
      status_message_ = "capture already in progress";
      return false;
    }

    next_frame_requested_ = true;
    capture_mode_ = CaptureMode::kNone;
    status_message_ = "next-frame capture armed";
    return true;
  }

  auto StartCapture(oxygen::observer_ptr<oxygen::graphics::Surface> surface)
    -> bool override
  {
    std::scoped_lock lock(mutex_);

    if (api_ == nullptr || IsCapturingUnlocked()) {
      status_message_ = api_ == nullptr ? "RenderDoc API unavailable"
                                        : "capture already in progress";
      return false;
    }

    next_frame_requested_ = false;
    if (!StartCaptureUnlocked(surface)) {
      return false;
    }

    capture_mode_ = CaptureMode::kManual;
    status_message_ = "manual capture started";
    return true;
  }

  auto EndCapture(oxygen::observer_ptr<oxygen::graphics::Surface> surface)
    -> bool override
  {
    std::scoped_lock lock(mutex_);

    if (api_ == nullptr || !IsCapturingUnlocked()) {
      status_message_ = api_ == nullptr ? "RenderDoc API unavailable"
                                        : "no active capture to end";
      return false;
    }

    const bool ended = EndCaptureUnlocked(surface);
    if (ended) {
      capture_mode_ = CaptureMode::kNone;
      next_frame_requested_ = false;
      status_message_ = "capture ended";
    }
    return ended;
  }

  auto DiscardCapture(oxygen::observer_ptr<oxygen::graphics::Surface> surface)
    -> bool override
  {
    std::scoped_lock lock(mutex_);

    if (api_ == nullptr) {
      status_message_ = "RenderDoc API unavailable";
      return false;
    }

    if (next_frame_requested_ && !IsCapturingUnlocked()) {
      next_frame_requested_ = false;
      capture_mode_ = CaptureMode::kNone;
      status_message_ = "next-frame capture request cleared";
      return true;
    }

    if (!IsCapturingUnlocked()) {
      status_message_ = "no active capture to discard";
      return false;
    }

    const bool discarded = DiscardCaptureUnlocked(surface);
    if (discarded) {
      capture_mode_ = CaptureMode::kNone;
      next_frame_requested_ = false;
      status_message_ = "capture discarded";
    }
    return discarded;
  }

  auto SetCaptureFileTemplate(const std::string_view path_template)
    -> bool override
  {
    std::scoped_lock lock(mutex_);

    config_.capture_file_template = std::string(path_template);
    if (api_ == nullptr || api_->SetCaptureFilePathTemplate == nullptr) {
      status_message_ = "RenderDoc API unavailable";
      return false;
    }

    api_->SetCaptureFilePathTemplate(config_.capture_file_template.c_str());
    status_message_ = "capture file template updated";
    return true;
  }

  auto LaunchReplayUI(const bool connect_target_control) -> bool override
  {
    std::scoped_lock lock(mutex_);

    if (api_ == nullptr || api_->LaunchReplayUI == nullptr) {
      status_message_ = "RenderDoc replay UI unavailable";
      return false;
    }

    if (api_->LaunchReplayUI(connect_target_control ? 1U : 0U, nullptr) == 0U) {
      status_message_ = "failed to launch RenderDoc replay UI";
      return false;
    }

    status_message_ = "RenderDoc replay UI launch requested";
    return true;
  }

  auto OnBeginFrame(oxygen::frame::SequenceNumber /*frame_number*/,
    oxygen::frame::Slot /*frame_slot*/) -> void override
  {
    std::scoped_lock lock(mutex_);

    if (api_ == nullptr || !next_frame_requested_ || IsCapturingUnlocked()) {
      return;
    }

    next_frame_requested_ = false;
    if (StartCaptureUnlocked({})) {
      capture_mode_ = CaptureMode::kNextFrame;
      status_message_ = "next-frame capture started";
    }
  }

  auto OnEndFrame(oxygen::frame::SequenceNumber /*frame_number*/,
    oxygen::frame::Slot /*frame_slot*/) -> void override
  {
    std::scoped_lock lock(mutex_);

    if (api_ == nullptr || capture_mode_ != CaptureMode::kNextFrame) {
      return;
    }

    if (EndCaptureUnlocked({})) {
      status_message_ = "next-frame capture completed";
    }
    capture_mode_ = CaptureMode::kNone;
  }

private:
  auto Initialize() -> void
  {
    if (config_.init_mode == oxygen::FrameCaptureInitMode::kDisabled) {
      status_message_ = "initialization disabled";
      return;
    }

    if (!TryInitializeFromConfig()) {
      return;
    }

    if (api_ != nullptr && !config_.capture_file_template.empty()) {
      api_->SetCaptureFilePathTemplate(config_.capture_file_template.c_str());
    }
    if (api_ != nullptr
      && config_.startup_trigger
        == oxygen::FrameCaptureStartupTrigger::kNextFrame) {
      next_frame_requested_ = true;
      status_message_ = "next-frame capture armed from startup config";
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
    target.device
      = static_cast<RENDERDOC_DevicePointer>(graphics_.GetCurrentDevice());

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
  bool next_frame_requested_ { false };
  CaptureMode capture_mode_ { CaptureMode::kNone };
};

} // namespace

namespace oxygen::graphics::d3d12 {

auto CreateRenderDocFrameCaptureController(
  Graphics& graphics, const FrameCaptureConfig& config)
  -> std::unique_ptr<graphics::FrameCaptureController>
{
  return std::make_unique<RenderDocFrameCaptureController>(graphics, config);
}

} // namespace oxygen::graphics::d3d12
