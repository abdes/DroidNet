//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <stdexcept>
#include <string>

#include "DemoShell/Test/UiTestSession.h"
#include <Windows.h> // IWYU pragma: keep
#include <imgui.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>
#include <imgui_te_exporters.h>
#include <minwindef.h>
#include <processenv.h>
#include <windef.h>
#include <wingdi.h>
#include <winuser.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Serio/FileStream.h>

namespace oxygen::examples::testing {
namespace {
  auto CompletedSuccessfully() -> bool&
  {
    static bool completed = false;
    return completed;
  }

  auto Environment(const char* name) -> std::string
  {
    const auto size = GetEnvironmentVariableA(name, nullptr, 0);
    if (size == 0) {
      return {};
    }
    std::string value(size, '\0');
    const auto copied = GetEnvironmentVariableA(name, value.data(), size);
    // A present but empty environment variable has size 1 and copies 0 bytes.
    if (copied >= size) {
      throw std::runtime_error("Cannot read UI test environment");
    }
    value.resize(copied);
    return value;
  }

  // Capture client-area evidence after presentation, checking its bounds and
  // corner visibility first. This is UI evidence, not a renderer measurement.
  auto SaveClientImage(void* native_window, const std::filesystem::path& path)
    -> bool
  {
    auto* const window = static_cast<HWND>(native_window);
    RECT client {};
    POINT origin {};
    if (GetClientRect(window, &client) == FALSE
      || ClientToScreen(window, &origin) == FALSE || client.right <= 0
      || client.bottom <= 0) {
      return false;
    }
    const auto width = client.right;
    const auto height = client.bottom;
    for (const auto point : {
           origin,
           POINT { .x = origin.x + width - 1, .y = origin.y + height - 1 },
         }) {
      if (GetAncestor(WindowFromPoint(point), GA_ROOT) != window) {
        return false;
      }
    }
    auto* const screen = GetDC(nullptr);
    if (!screen) {
      return false;
    }
    const auto release
      = ScopeGuard([&] noexcept -> void { ReleaseDC(nullptr, screen); });
    auto* const memory = CreateCompatibleDC(screen);
    if (!memory) {
      return false;
    }
    const auto release_memory
      = ScopeGuard([&] noexcept -> void { DeleteDC(memory); });
    BITMAPINFO info {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    auto* const bitmap
      = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) {
      return false;
    }
    const auto release_bitmap
      = ScopeGuard([&] noexcept -> void { DeleteObject(bitmap); });
    auto* const previous = SelectObject(memory, bitmap);
    const auto restore
      = ScopeGuard([&] noexcept -> void { SelectObject(memory, previous); });
    if (BitBlt(memory, 0, 0, width, height, screen, origin.x, origin.y, SRCCOPY)
      == FALSE) {
      return false;
    }
    GdiFlush();
    BITMAPFILEHEADER header {};
    constexpr WORD kBitmapSignature = 0x4d42;
    header.bfType = kBitmapSignature;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    const auto byte_count
      = static_cast<size_t>(width) * height * sizeof(unsigned int);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(byte_count);
    serio::FileStream<> file(path, std::ios::out);
    return file.Write(std::as_bytes(std::span(&header, 1)))
      && file.Write(std::as_bytes(std::span(&info.bmiHeader, 1)))
      && file.Write(std::span(static_cast<const std::byte*>(bits), byte_count))
      && file.Flush();
  }
} // namespace

auto UiTestSession::Requested() -> bool
{
  return !Environment("OXYGEN_UI_TEST_OUTPUT").empty();
}
auto UiTestSession::ExitCode(const int application_exit_code) -> int
{
  return application_exit_code != 0 || (Requested() && !CompletedSuccessfully())
    ? EXIT_FAILURE
    : EXIT_SUCCESS;
}

UiTestSession::UiTestSession(ImGuiContext& context, void* native_window)
  : output_(std::filesystem::absolute(Environment("OXYGEN_UI_TEST_OUTPUT")))
  , report_((output_ / "results.xml").string())
  , native_window_(native_window)
{
  std::filesystem::create_directories(output_);
  std::filesystem::create_directories(output_ / "failures");
  ImGui::SetCurrentContext(&context);
  ImGui::GetIO().IniFilename = nullptr;
  // The target context must be bound before creating Test Engine (multiple
  // DLLs may each link ImGui). Creation cannot move to a member initializer.
  // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer)
  engine_ = ImGuiTestEngine_CreateContext();
  const auto rollback = ScopeGuard([this] noexcept -> void {
    if (!started_) {
      ImGuiTestEngine_DestroyContext(engine_);
    }
  });
  auto& io = ImGuiTestEngine_GetIO(engine_);
  io.ConfigSavedSettings = false;
  io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
  io.ConfigLogToTTY = true;
  io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
  io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
  // Conan's Test Engine package omits its optional PNG capture backend. Save a
  // native BMP at the post-present boundary instead of reporting an empty PNG.
  io.ConfigCaptureOnError = false;
  io.ConfigLogToFuncUserData = this;
  io.ConfigLogToFunc
    = [](ImGuiTestEngine*, ImGuiTestContext* ctx, ImGuiTestVerboseLevel level,
        const char*, void* data) -> void {
    auto& session = *static_cast<UiTestSession*>(data);
    if (level == ImGuiTestVerboseLevel_Error && ctx
      && session.pending_failure_.empty()) {
      session.pending_failure_ = ctx->Test->Name;
    }
  };
  io.ConfigBreakOnError = false;
  constexpr float kTestTimeoutSeconds = 30.0F;
  io.ConfigWatchdogKillTest = kTestTimeoutSeconds;
  io.ExportResultsFilename = report_.c_str();
  io.ExportResultsFormat = ImGuiTestEngineExportFormat_JUnitXml;
  ImGuiTestEngine_Start(engine_, &context);
  // Arm cleanup only after Start succeeds; rollback covers partial creation.
  // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer)
  started_ = true;
}

UiTestSession::~UiTestSession()
{
  if (started_) {
    ImGuiTestEngine_Stop(engine_);
  }
  ImGuiTestEngine_DestroyContext(engine_);
}

auto UiTestSession::Start() -> void
{
  const auto filter = Environment("OXYGEN_UI_TEST_FILTER");
  ImGuiTestEngine_QueueTests(
    engine_, ImGuiTestGroup_Tests, filter.empty() ? nullptr : filter.c_str());
}

auto UiTestSession::AfterPresent() -> bool
{
  if (finished_) {
    return true;
  }
  ImGuiTestEngine_PostSwap(engine_);
  if (!pending_failure_.empty()) {
    const auto path = output_ / "failures"
      / (pending_failure_ + "-" + std::to_string(++failure_index_) + ".bmp");
    if (!SaveClientImage(native_window_, path)) {
      std::ofstream reason(path.string() + ".txt");
      reason << "Capture unavailable: client area was occluded or native "
                "capture failed.\n";
      LOG_F(WARNING, "UI failure image unavailable: {}", path.string());
    }
    pending_failure_.clear();
  }
  if (!ImGuiTestEngine_IsTestQueueEmpty(engine_)) {
    return false;
  }
  ImGuiTestEngineResultSummary result {};
  ImGuiTestEngine_GetResultSummary(engine_, &result);
  CompletedSuccessfully()
    = result.CountTested > 0 && result.CountTested == result.CountSuccess;
  ImGuiTestEngine_Stop(engine_);
  started_ = false;
  if (!SaveClientImage(native_window_, output_ / "final-state.bmp")) {
    std::ofstream reason(output_ / "final-state-unavailable.txt");
    reason << "Client area was occluded or native capture failed.\n";
  }
  std::ofstream summary(output_ / "summary.txt");
  summary << "tested=" << result.CountTested
          << " passed=" << result.CountSuccess
          << " success=" << CompletedSuccessfully() << '\n';
  LOG_F(INFO, "UI tests: {} tested, {} passed; {}", result.CountTested,
    result.CountSuccess, report_);
  finished_ = true;
  return true;
}
} // namespace oxygen::examples::testing
