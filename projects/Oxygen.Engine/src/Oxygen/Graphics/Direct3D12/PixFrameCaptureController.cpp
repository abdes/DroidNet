//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cctype>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <windows.h>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/PixFrameCaptureController.h>

#if defined(USE_PIX) && __has_include(<pix3.h>)
#  include <pix3.h>
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

constexpr wchar_t kPixGpuCapturerModuleName[] = L"WinPixGpuCapturer.dll";

constexpr auto kPixSupportedFeatures
  = oxygen::graphics::FrameCaptureFeature::kTriggerNextFrame
  | oxygen::graphics::FrameCaptureFeature::kManualCapture
  | oxygen::graphics::FrameCaptureFeature::kCaptureFileTemplate;

constexpr auto IsPixMarkersBuildAvailable() noexcept -> bool
{
#if defined(USE_PIX) && __has_include(<pix3.h>)
  return true;
#else
  return false;
#endif
}

constexpr auto IsPixGpuCaptureDiscoveryAvailable() noexcept -> bool
{
#if defined(OXYGEN_PIX_GPU_CAPTURE_AVAILABLE)
  return true;
#else
  return false;
#endif
}

constexpr auto IsPixTimingCaptureDiscoveryAvailable() noexcept -> bool
{
#if defined(OXYGEN_PIX_TIMING_CAPTURE_AVAILABLE)
  return true;
#else
  return false;
#endif
}

constexpr auto IsPixUiAvailable() noexcept -> bool
{
#if defined(OXYGEN_PIX_UI_AVAILABLE)
  return true;
#else
  return false;
#endif
}

auto PixInstallRoot() -> std::string_view
{
#if defined(OXYGEN_PIX_INSTALL_ROOT_PATH)
  return OXYGEN_PIX_INSTALL_ROOT_PATH;
#else
  return {};
#endif
}

auto LastErrorMessage(const std::string_view action) -> std::string
{
  return std::string(action) + " failed with Win32 error "
    + std::to_string(::GetLastError());
}

auto HResultMessage(const std::string_view action, const HRESULT hr)
  -> std::string
{
  return fmt::format(
    "{} failed (hr=0x{:08X})", action, static_cast<unsigned int>(hr));
}

auto ToWidePath(const std::string_view path) -> std::wstring
{
  return std::filesystem::path(std::string(path)).wstring();
}

auto NarrowPath(const std::filesystem::path& path) -> std::string
{
  return path.generic_string();
}

auto ToEngineFrameSequence(const uint64_t zero_based_frame_index) -> uint64_t
{
  return zero_based_frame_index + 1U;
}

auto SanitizeCaptureLabel(std::string_view label) -> std::string
{
  std::string sanitized;
  sanitized.reserve(label.size());
  for (const char ch : label) {
    if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
      sanitized.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    } else if (ch == '_' || ch == '-') {
      sanitized.push_back(ch);
    } else {
      sanitized.push_back('_');
    }
  }
  if (sanitized.empty()) {
    sanitized = "capture";
  }
  return sanitized;
}

auto ResolveModulePath(const HMODULE module) -> std::string
{
  wchar_t buffer[MAX_PATH] {};
  const auto length = ::GetModuleFileNameW(module, buffer, MAX_PATH);
  if (length == 0U) {
    return {};
  }
  return NarrowPath(std::filesystem::path(buffer));
}

#if defined(USE_PIX) && __has_include(<pix3.h>)

class PixFrameCaptureController final
  : public oxygen::graphics::FrameCaptureController {
public:
  PixFrameCaptureController(oxygen::graphics::d3d12::Graphics& graphics,
    oxygen::FrameCaptureConfig config)
    : graphics_(graphics)
    , config_(std::move(config))
  {
    Initialize();
  }

  ~PixFrameCaptureController() override
  {
    if (owns_module_ && module_handle_ != nullptr) {
      ::FreeLibrary(module_handle_);
      module_handle_ = nullptr;
    }
  }

  [[nodiscard]] auto GetProviderName() const noexcept
    -> std::string_view override
  {
    return "PIX";
  }

  [[nodiscard]] auto IsAvailable() const noexcept -> bool override
  {
    std::scoped_lock lock(mutex_);
    return HasLoadedCapturerUnlocked();
  }

  [[nodiscard]] auto IsCapturing() const noexcept -> bool override
  {
    std::scoped_lock lock(mutex_);
    return IsCapturingUnlocked();
  }

  [[nodiscard]] auto GetSupportedFeatures() const noexcept
    -> oxygen::graphics::FrameCaptureFeature override
  {
    return kPixSupportedFeatures;
  }

  [[nodiscard]] auto DescribeState() const -> std::string override
  {
    std::scoped_lock lock(mutex_);

    std::ostringstream out;
    out << "provider=PIX"
        << " configured=true"
        << " markers_available="
        << (IsPixMarkersBuildAvailable() ? "true" : "false")
        << " gpu_capture_available="
        << (IsPixGpuCaptureDiscoveryAvailable() ? "true" : "false")
        << " timing_capture_available="
        << (IsPixTimingCaptureDiscoveryAvailable() ? "true" : "false")
        << " pix_ui_available=" << (IsPixUiAvailable() ? "true" : "false")
        << " capturer_loaded="
        << (HasLoadedCapturerUnlocked() ? "true" : "false") << " attached="
        << (IsAttachedForGpuCaptureUnlocked() ? "true" : "false")
        << " capturing=" << (IsCapturingUnlocked() ? "true" : "false")
        << " capture_from_frame=" << scheduled_capture_from_frame_
        << " capture_frame_count=" << scheduled_capture_frame_count_
        << " active_target_window="
        << (last_target_window_ != nullptr ? "true" : "false")
        << " features=" << DescribeSupportedFeatures()
        << " init_mode=" << InitModeText(config_.init_mode);

    if (!config_.module_path.empty()) {
      out << " configured_module=" << config_.module_path;
    }
    if (!resolved_module_path_.empty()) {
      out << " module=" << resolved_module_path_;
    }
    if (!PixInstallRoot().empty()) {
      out << " install_root=" << PixInstallRoot();
    }
    if (!last_capture_file_path_.empty()) {
      out << " last_capture_file=" << last_capture_file_path_;
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
    return RequestNextFramesCaptureUnlocked(
      1U, "PIX next-frame capture requested", "frame");
  }

  auto DoStartCapture(oxygen::observer_ptr<oxygen::graphics::Surface> surface)
    -> bool override
  {
    std::scoped_lock lock(mutex_);

    if (!EnsureGpuCaptureReadyUnlocked()) {
      LOG_F(WARNING, "PIX manual capture rejected: {}", status_message_);
      return false;
    }
    if (IsCapturingUnlocked()) {
      status_message_ = "capture already in progress";
      LOG_F(WARNING, "PIX manual capture rejected: {}", status_message_);
      return false;
    }

    const auto target_window
      = surface != nullptr ? ResolveTargetWindow(surface) : nullptr;
    if (target_window != nullptr) {
      last_target_window_ = target_window;
    }
    if (!ApplyTargetWindowUnlocked()) {
      LOG_F(WARNING,
        "PIX manual capture started without an observed target window; PIX "
        "will use its active window");
    }

    const auto capture_path = BuildCaptureFilePathUnlocked("manual");
    PIXCaptureParameters capture_params {};
    capture_params.GpuCaptureParameters.FileName = capture_path.c_str();

    const auto hr = PIXBeginCapture(PIX_CAPTURE_GPU, &capture_params);
    if (FAILED(hr)) {
      status_message_ = HResultMessage("PIXBeginCapture", hr);
      LOG_F(WARNING, "PIX manual capture failed: {}", status_message_);
      return false;
    }

    manual_capture_active_ = true;
    last_capture_file_path_ = NarrowPath(std::filesystem::path(capture_path));
    status_message_ = "manual capture started -> " + last_capture_file_path_;
    LOG_F(INFO, "PIX manual capture started: {}", last_capture_file_path_);
    return true;
  }

  auto DoEndCapture(oxygen::observer_ptr<oxygen::graphics::Surface> /*surface*/)
    -> bool override
  {
    std::scoped_lock lock(mutex_);

    if (!EnsureGpuCaptureReadyUnlocked()) {
      LOG_F(WARNING, "PIX capture end rejected: {}", status_message_);
      return false;
    }
    if (!IsCapturingUnlocked()) {
      status_message_ = "no active capture to end";
      LOG_F(WARNING, "PIX capture end rejected: {}", status_message_);
      return false;
    }

    const auto hr = PIXEndCapture(FALSE);
    if (FAILED(hr)) {
      status_message_ = HResultMessage("PIXEndCapture", hr);
      LOG_F(WARNING, "PIX capture end failed: {}", status_message_);
      return false;
    }

    manual_capture_active_ = false;
    status_message_ = "capture ended";
    LOG_F(INFO, "PIX capture ended");
    return true;
  }

  auto DoDiscardCapture(
    oxygen::observer_ptr<oxygen::graphics::Surface> /*surface*/)
    -> bool override
  {
    return false;
  }

  auto DoSetCaptureFileTemplate(const std::string_view path_template)
    -> bool override
  {
    std::scoped_lock lock(mutex_);
    config_.capture_file_template = std::string(path_template);
    status_message_ = "capture file template updated";
    LOG_F(INFO, "PIX capture file template updated: {}",
      config_.capture_file_template);
    return true;
  }

  auto DoLaunchReplayUI(bool /*connect_target_control*/) -> bool override
  {
    return false;
  }

  auto OnBeginFrame(oxygen::frame::SequenceNumber frame_number,
    oxygen::frame::Slot /*frame_slot*/) -> void override
  {
    std::scoped_lock lock(mutex_);

    if (scheduled_capture_frame_count_ == 0
      || frame_number.get()
        != ToEngineFrameSequence(scheduled_capture_from_frame_)) {
      return;
    }

    const auto requested_from_frame = scheduled_capture_from_frame_;
    const auto requested_frame_count = scheduled_capture_frame_count_;
    scheduled_capture_from_frame_ = 0;
    scheduled_capture_frame_count_ = 0;

    const auto label = requested_frame_count == 1 ? "frame" : "frames";
    const auto action = fmt::format(
      "PIX configured frame-range capture requested from frame {} for {} "
      "frame(s)",
      requested_from_frame, requested_frame_count);
    if (!RequestNextFramesCaptureUnlocked(
          requested_frame_count, action, label)) {
      LOG_F(WARNING, "{}", action);
    }
  }

  auto OnEndFrame(oxygen::frame::SequenceNumber /*frame_number*/,
    oxygen::frame::Slot /*frame_slot*/) -> void override
  {
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

    last_target_window_ = ResolveTargetWindow(surface);
    if (HasLoadedCapturerUnlocked()) {
      (void)ApplyTargetWindowUnlocked();
    }
  }

  auto Initialize() -> void
  {
    if (config_.init_mode == oxygen::FrameCaptureInitMode::kDisabled) {
      status_message_ = "initialization disabled";
      LOG_F(INFO, "PIX frame capture initialization disabled");
    } else if (!EnsureGpuCaptureReadyUnlocked()) {
      LOG_F(WARNING, "PIX frame capture initialization failed: {}",
        status_message_);
    }

    if (!config_.capture_file_template.empty()) {
      LOG_F(INFO, "PIX capture file template configured: {}",
        config_.capture_file_template);
    }
    if (config_.frame_count > 0) {
      if (config_.from_frame == 0) {
        status_message_
          = "configured PIX startup capture requires from_frame > 0";
        LOG_F(WARNING, "PIX configured startup capture rejected: {}",
          status_message_);
        return;
      }
      scheduled_capture_from_frame_ = config_.from_frame;
      scheduled_capture_frame_count_ = config_.frame_count;
      const auto armed_message = fmt::format(
        "configured frame-range capture armed from frame {} for {} frame(s)",
        config_.from_frame, config_.frame_count);
      if (HasLoadedCapturerUnlocked()) {
        status_message_ = armed_message;
      }
      LOG_F(INFO, "PIX {}", armed_message);
    }
  }

  auto EnsureGpuCaptureReadyUnlocked() -> bool
  {
    if (HasLoadedCapturerUnlocked()) {
      if (module_handle_ == nullptr) {
        if (const auto attached = ::GetModuleHandleW(kPixGpuCapturerModuleName);
          attached != nullptr) {
          return BindCapturerModuleUnlocked(
            attached, false, "attached PIX GPU capturer");
        }
      }
      return true;
    }

    switch (config_.init_mode) {
    case oxygen::FrameCaptureInitMode::kDisabled:
      status_message_ = "initialization disabled";
      return false;
    case oxygen::FrameCaptureInitMode::kAttachedOnly: {
      if (const auto attached = ::GetModuleHandleW(kPixGpuCapturerModuleName);
        attached != nullptr) {
        return BindCapturerModuleUnlocked(
          attached, false, "attached PIX GPU capturer");
      }
      status_message_ = "PIX GPU capturer is not loaded in this process";
      return false;
    }
    case oxygen::FrameCaptureInitMode::kSearchPath: {
      if (const auto attached = ::GetModuleHandleW(kPixGpuCapturerModuleName);
        attached != nullptr) {
        return BindCapturerModuleUnlocked(
          attached, false, "attached PIX GPU capturer");
      }
      const auto module = PIXLoadLatestWinPixGpuCapturerLibrary();
      if (module == nullptr) {
        status_message_
          = LastErrorMessage("PIXLoadLatestWinPixGpuCapturerLibrary");
        return false;
      }
      return BindCapturerModuleUnlocked(
        module, true, "PIX GPU capturer loaded from installed PIX search path");
    }
    case oxygen::FrameCaptureInitMode::kExplicitPath: {
      if (const auto attached = ::GetModuleHandleW(kPixGpuCapturerModuleName);
        attached != nullptr) {
        return BindCapturerModuleUnlocked(
          attached, false, "attached PIX GPU capturer");
      }
      if (config_.module_path.empty()) {
        status_message_ = "explicit PIX GPU capturer path not provided";
        return false;
      }
      const auto module
        = ::LoadLibraryW(ToWidePath(config_.module_path).c_str());
      if (module == nullptr) {
        status_message_
          = LastErrorMessage("LoadLibraryW(explicit PIX GPU capturer path)");
        return false;
      }
      return BindCapturerModuleUnlocked(
        module, true, "PIX GPU capturer loaded from explicit path");
    }
    }

    status_message_ = "unsupported PIX initialization mode";
    return false;
  }

  auto BindCapturerModuleUnlocked(HMODULE module, const bool owns_module,
    const std::string_view source) -> bool
  {
    if (module == nullptr) {
      status_message_ = "PIX GPU capturer module handle is null";
      return false;
    }

    module_handle_ = module;
    owns_module_ = owns_module;
    resolved_module_path_ = ResolveModulePath(module);
    status_message_ = std::string(source) + " ready";

    LOG_F(INFO,
      "PIX frame capture controller ready: module='{}' markers_available={} "
      "gpu_capture_available={} timing_capture_available={} "
      "pix_ui_available={}",
      resolved_module_path_, IsPixMarkersBuildAvailable(),
      IsPixGpuCaptureDiscoveryAvailable(),
      IsPixTimingCaptureDiscoveryAvailable(), IsPixUiAvailable());

    if (last_target_window_ != nullptr && !ApplyTargetWindowUnlocked()) {
      LOG_F(WARNING, "PIX target window setup failed during initialization: {}",
        status_message_);
    }

    return true;
  }

  [[nodiscard]] auto HasLoadedCapturerUnlocked() const noexcept -> bool
  {
    return module_handle_ != nullptr
      || ::GetModuleHandleW(kPixGpuCapturerModuleName) != nullptr;
  }

  [[nodiscard]] auto IsAttachedForGpuCaptureUnlocked() const noexcept -> bool
  {
    return HasLoadedCapturerUnlocked() && PIXIsAttachedForGpuCapture();
  }

  [[nodiscard]] auto IsCapturingUnlocked() const noexcept -> bool
  {
    if (!HasLoadedCapturerUnlocked()) {
      return false;
    }
    const auto capture_state = PIXGetCaptureState();
    return (capture_state & PIX_CAPTURE_GPU) != 0U || manual_capture_active_;
  }

  auto ApplyTargetWindowUnlocked() -> bool
  {
    if (!HasLoadedCapturerUnlocked() || last_target_window_ == nullptr) {
      return false;
    }

    const auto hr = PIXSetTargetWindow(last_target_window_);
    if (FAILED(hr)) {
      status_message_ = HResultMessage("PIXSetTargetWindow", hr);
      return false;
    }

    return true;
  }

  auto RequestNextFramesCaptureUnlocked(const uint32_t frame_count,
    const std::string& action_text, const std::string_view capture_label)
    -> bool
  {
    if (frame_count == 0) {
      status_message_ = "frame count must be greater than 0";
      LOG_F(WARNING, "PIX capture request rejected: {}", status_message_);
      return false;
    }
    if (!EnsureGpuCaptureReadyUnlocked()) {
      LOG_F(WARNING, "PIX capture request rejected: {}", status_message_);
      return false;
    }
    if (IsCapturingUnlocked()) {
      status_message_ = "capture already in progress";
      LOG_F(WARNING, "PIX capture request rejected: {}", status_message_);
      return false;
    }

    if (!ApplyTargetWindowUnlocked()) {
      LOG_F(WARNING,
        "PIX capture requested without an observed target window; PIX will "
        "use its active window");
    }

    const auto capture_path = BuildCaptureFilePathUnlocked(capture_label);
    const auto hr = PIXGpuCaptureNextFrames(capture_path.c_str(), frame_count);
    if (FAILED(hr)) {
      status_message_ = HResultMessage("PIXGpuCaptureNextFrames", hr);
      LOG_F(WARNING, "PIX capture request failed: {}", status_message_);
      return false;
    }

    last_capture_file_path_ = NarrowPath(std::filesystem::path(capture_path));
    status_message_ = action_text + " -> " + last_capture_file_path_;
    LOG_F(INFO, "{}: {}", action_text, last_capture_file_path_);
    return true;
  }

  [[nodiscard]] auto BuildCaptureFilePathUnlocked(
    const std::string_view capture_label)
  {
    const auto serial = ++capture_request_serial_;
    const auto label = SanitizeCaptureLabel(capture_label);

    auto template_path = config_.capture_file_template.empty()
      ? std::filesystem::path("captures") / "pix_capture"
      : std::filesystem::path(config_.capture_file_template);

    std::filesystem::path directory = template_path.parent_path();
    std::string stem = template_path.stem().string();
    if (!template_path.has_filename()) {
      directory = template_path;
      stem.clear();
    }
    if (stem.empty()) {
      stem = "pix_capture";
    }

    const auto filename = fmt::format("{}_{}_{:04}.wpix", stem, label, serial);
    auto capture_path = directory / filename;

    std::error_code ec;
    if (!capture_path.parent_path().empty()) {
      std::filesystem::create_directories(capture_path.parent_path(), ec);
      if (ec) {
        LOG_F(WARNING, "PIX could not create capture directory '{}': {}",
          capture_path.parent_path().generic_string(), ec.message());
      }
    }

    return capture_path.wstring();
  }

  [[nodiscard]] auto ResolveTargetWindow(
    const oxygen::observer_ptr<oxygen::graphics::Surface> surface) const -> HWND
  {
    if (surface == nullptr) {
      return nullptr;
    }
    if (!surface->HasComponent<oxygen::graphics::detail::WindowComponent>()) {
      return nullptr;
    }
    const auto handles
      = surface->GetComponent<oxygen::graphics::detail::WindowComponent>()
          .Native();
    return static_cast<HWND>(handles.window_handle);
  }

  oxygen::graphics::d3d12::Graphics& graphics_;
  oxygen::FrameCaptureConfig config_;
  HMODULE module_handle_ { nullptr };
  bool owns_module_ { false };
  bool manual_capture_active_ { false };
  std::string resolved_module_path_ {};
  std::string status_message_ {};
  std::string last_capture_file_path_ {};
  mutable std::mutex mutex_ {};
  HWND last_target_window_ { nullptr };
  uint64_t scheduled_capture_from_frame_ { 0 };
  uint32_t scheduled_capture_frame_count_ { 0 };
  uint64_t capture_request_serial_ { 0 };
};

#else

class PixFrameCaptureController final
  : public oxygen::graphics::FrameCaptureController {
public:
  PixFrameCaptureController(oxygen::graphics::d3d12::Graphics& /*graphics*/,
    oxygen::FrameCaptureConfig config)
    : config_(std::move(config))
  {
    status_message_ = "PIX support unavailable in this build";
  }

  [[nodiscard]] auto GetProviderName() const noexcept
    -> std::string_view override
  {
    return "PIX";
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
    return kPixSupportedFeatures;
  }

  [[nodiscard]] auto DescribeState() const -> std::string override
  {
    std::ostringstream out;
    out << "provider=PIX"
        << " configured=true"
        << " markers_available="
        << (IsPixMarkersBuildAvailable() ? "true" : "false")
        << " gpu_capture_available="
        << (IsPixGpuCaptureDiscoveryAvailable() ? "true" : "false")
        << " timing_capture_available="
        << (IsPixTimingCaptureDiscoveryAvailable() ? "true" : "false")
        << " pix_ui_available=" << (IsPixUiAvailable() ? "true" : "false")
        << " capturer_loaded=false"
        << " attached=false"
        << " capturing=false"
        << " capture_from_frame=" << config_.from_frame
        << " capture_frame_count=" << config_.frame_count
        << " active_target_window=false"
        << " features=" << DescribeSupportedFeatures()
        << " init_mode=" << InitModeText(config_.init_mode);
    if (!config_.module_path.empty()) {
      out << " configured_module=" << config_.module_path;
    }
    if (!PixInstallRoot().empty()) {
      out << " install_root=" << PixInstallRoot();
    }
    if (!status_message_.empty()) {
      out << " status=\"" << status_message_ << '"';
    }
    return out.str();
  }

private:
  auto DoTriggerNextFrame() -> bool override { return false; }

  auto DoStartCapture(
    oxygen::observer_ptr<oxygen::graphics::Surface> /*surface*/)
    -> bool override
  {
    return false;
  }

  auto DoEndCapture(oxygen::observer_ptr<oxygen::graphics::Surface> /*surface*/)
    -> bool override
  {
    return false;
  }

  auto DoDiscardCapture(
    oxygen::observer_ptr<oxygen::graphics::Surface> /*surface*/)
    -> bool override
  {
    return false;
  }

  auto DoSetCaptureFileTemplate(std::string_view path_template) -> bool override
  {
    config_.capture_file_template = std::string(path_template);
    return true;
  }

  auto DoLaunchReplayUI(bool /*connect_target_control*/) -> bool override
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

auto CreatePixFrameCaptureController(
  Graphics& graphics, const FrameCaptureConfig& config)
  -> std::unique_ptr<graphics::FrameCaptureController>
{
  return std::make_unique<PixFrameCaptureController>(graphics, config);
}

} // namespace oxygen::graphics::d3d12
