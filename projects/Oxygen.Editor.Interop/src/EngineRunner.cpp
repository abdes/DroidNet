//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma managed

#include "EngineRunner.h"
#include "SimpleEditorModule.h"
#include "Base/LoguruWrapper.h"

#include <fmt/format.h>
#include <msclr/marshal.h>
#include <msclr/marshal_cppstd.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/EditorInterface/Api.h>

#include <dxgi1_2.h>
#include <string>
#include <chrono>
#include <ctime>
#include <vcclr.h>
#include <mutex>
#include <unordered_map>
#include <functional>

// tokens_map and helpers declared below once System namespace is available.

// WinUI 3 ISwapChainPanelNative definition (desktop IID)
struct __declspec(uuid("63AAD0B8-7C24-40FF-85A8-640D944CC325")) ISwapChainPanelNative : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE SetSwapChain(IDXGISwapChain* swapChain) = 0;
};

using namespace System;
using namespace System::Diagnostics;
using namespace System::Threading;
using namespace System::Threading::Tasks;
using namespace Microsoft::Extensions::Logging;
namespace InteropLogging = Oxygen::Interop::Logging;

// Map native logging verbosity to Microsoft.Extensions.Logging.LogLevel
// integer.
static auto MapVerbosityToManagedLevel(int verbosity) -> LogLevel {
  if (verbosity <= -3) {
    return LogLevel::Critical;
  }
  if (verbosity == -2) {
    return LogLevel::Error;
  }
  if (verbosity == -1) {
    return LogLevel::Warning;
  }
  if (verbosity == 0) {
    return LogLevel::Information;
  }
  if (verbosity == 1) {
    return LogLevel::Debug;
  }
  return LogLevel::Trace;
}

// Forward declare the indirection function; it's implemented after LogHandler
// so it can safely cast to LogHandler and call its instance method.
static void InvokeLogHandler(Object^ obj, const loguru::Message& msg);

// Native function registered with loguru. Declared here so LogHandler can call
// add_callback with its address. It will obtain the GCHandle target and then
// call InvokeLogHandler.
static void NativeForward(void* user_data, const loguru::Message& msg) {
  try {
    if (!user_data) {
      return;
    }
    Runtime::InteropServices::GCHandle handle =
      Runtime::InteropServices::GCHandle::FromIntPtr(IntPtr(user_data));
    if (!handle.IsAllocated) {
      return;
    }
    auto target = handle.Target;
    if (target != nullptr) {
      InvokeLogHandler(target, msg);
    }
  }
  catch (...) {
    /* swallow */
  }
}

namespace Oxygen::Editor::EngineInterface {

  // Token storage: native map keyed by the registry GuidKey, storing a pointer
  // to a GCHandle (via IntPtr.ToPointer()). The GCHandle holds the
  // TaskCompletionSource kept alive until the engine module processes the
  // pending destruction/resize and we resolve the token.
  using TokenKey = SurfaceRegistry::GuidKey;
  // SurfaceRegistry::GuidHasher is a private helper; provide an internal
  // TokenHasher for use in this compilation unit.
  struct TokenHasher {
    auto operator()(const TokenKey& key) const noexcept -> std::size_t {
      std::size_t hash = 1469598103934665603ULL;
      for (auto byte : key) {
        hash ^= static_cast<std::size_t>(byte);
        hash *= 1099511628211ULL;
      }
      return hash;
    }
  };
  static std::unordered_map<TokenKey, void*, TokenHasher> tokens_map;
  static std::mutex tokens_mutex;

  static void ResolveToken(const TokenKey& nativeKey, bool ok)
  {
    // Helpful diagnostic message for interop troubleshooting: include a
    // short hex representation of the token key and whether resolution
    // succeeded.
    auto tokenToHex = [](const TokenKey &k) -> std::string {
      std::string out;
      out.reserve(k.size() * 3);
      for (size_t i = 0; i < k.size(); ++i) {
        out += fmt::format("{:02x}", static_cast<unsigned int>(static_cast<unsigned char>(k[i])));
        if (i + 1 < k.size() && (i % 4 == 3)) out.push_back('-');
      }
      return out;
    };
    try {
      auto msg = fmt::format("ResolveToken: key={} ok={}", tokenToHex(nativeKey), ok ? "true" : "false");
      oxygen::engine::interop::LogInfoMessage(msg.c_str());
    }
    catch (...) { /* keep resolving even if logging fails */ }
    std::lock_guard<std::mutex> lg(tokens_mutex);
    auto it = tokens_map.find(nativeKey);
    if (it == tokens_map.end()) {
      return;
    }

    void* hv = it->second;
    if (hv != nullptr) {
      try {
        System::IntPtr ip(hv);
        auto gh = System::Runtime::InteropServices::GCHandle::FromIntPtr(ip);
        auto tcs = safe_cast<System::Threading::Tasks::TaskCompletionSource<bool>^>(gh.Target);
        if (tcs != nullptr) {
          tcs->TrySetResult(ok);
        }
        gh.Free();
      }
      catch (...) {
        /* swallow */
      }
    }

    tokens_map.erase(it);
  }

  // Helper that returns a native callback which resolves the given token.
  static std::function<void(bool)> MakeResolveCallback(const TokenKey& k) {
    // copy the token into the heap-allocated closure so we don't create a
    // local class inside a managed member function.
    TokenKey copy = k;
    return [copy](bool ok) { ResolveToken(copy, ok); };
  }

  // Managed helper that encapsulates all logging-related state and behavior so
  // the EngineRunner header doesn't need to include or reference native logging
  // internals.
  public
  ref class LogHandler sealed {
  public:
    LogHandler()
      : logger_(nullptr)
      , callback_registered_(false)
      , self_handle_(IntPtr::Zero) {
      Runtime::InteropServices::GCHandle h =
        Runtime::InteropServices::GCHandle::Alloc(
          this,
          Runtime::InteropServices::GCHandleType::WeakTrackResurrection);
      self_handle_ = Runtime::InteropServices::GCHandle::ToIntPtr(h);
    }

    ~LogHandler() { ReleaseCallback(); }

    !LogHandler() { ReleaseCallback(); }

    void SetLogger(Object^ logger) {
      logger_ = nullptr;
      if (logger == nullptr) {
        return;
      }

      auto ilogger = dynamic_cast<ILogger^>(logger);
      if (ilogger == nullptr) {
        throw gcnew ArgumentException(
          "logger must implement Microsoft.Extensions.Logging.ILogger",
          "logger");
      }

      logger_ = ilogger;
    }

    bool ConfigureLogging(LoggingConfig^ config) {
      namespace op = oxygen::engine::interop;
      op::LoggingConfig native_config{};
      native_config.verbosity = config->Verbosity;
      native_config.is_colored = config->IsColored;
      native_config.vmodules = nullptr;
      std::string vmodules;
      if (config->ModuleOverrides != nullptr) {
        vmodules =
          msclr::interop::marshal_as<std::string>(config->ModuleOverrides);
        if (!vmodules.empty()) {
          native_config.vmodules = vmodules.c_str();
        }
      }
      bool ok = op::ConfigureLogging(native_config);
      if (ok) {
        RegisterCallbackIfNeeded();
        InteropLogging::Loguru::WriteAndFlush(
          InteropLogging::Loguru::Verbosity::Verbosity_INFO,
          gcnew String(L"Oxygen Editor logging configured."));
      }
      return ok;
    }

    // Invoked from native forwarder through the GCHandle.
    void HandleLog(const loguru::Message& message) {
      try {
        std::string composed;
        if (message.preamble && *message.preamble) {
          composed += message.preamble;
          composed += ' ';
        }
        if (message.prefix && *message.prefix) {
          composed += message.prefix;
        }
        if (message.message && *message.message) {
          composed += message.message;
        }
        auto managedMsg = gcnew String(composed.c_str());
#if defined(_DEBUG) || !defined(NDEBUG)
        if (logger_ == nullptr) {
          Debug::WriteLine(managedMsg);
          return;
        }
#else
        if (logger_ == nullptr)
          return;
#endif

        LogLevel lvlValue = MapVerbosityToManagedLevel(message.verbosity);

        if (logger_ != nullptr) {
          logger_->Log<String^>(lvlValue, EventId(0), managedMsg, nullptr,
            gcnew Func<String^, Exception^, String^>(&LogHandler::Format));
        }
      }
      catch (...) {
        /* swallow */
      }
    }

    static String^ Format(String^ state, Exception^ ex) {
      return state;
    }

  private:
    void RegisterCallbackIfNeeded() {
      if (callback_registered_) {
        return;
      }
      // Register the native forwarder function with loguru.
      loguru::add_callback("OxygenEditorManagedLogger", &NativeForward,
        self_handle_.ToPointer(), loguru::Verbosity_MAX);
      callback_registered_ = true;
    }

    void ReleaseCallback() {
      if (callback_registered_) {
        loguru::remove_callback("OxygenEditorManagedLogger");
        callback_registered_ = false;
      }
      if (self_handle_ != IntPtr::Zero) {
        Runtime::InteropServices::GCHandle h =
          Runtime::InteropServices::GCHandle::FromIntPtr(self_handle_);
        if (h.IsAllocated) {
          h.Free();
        }
        self_handle_ = IntPtr::Zero;
      }
    }

    // Instance state
    ILogger^ logger_;
    bool callback_registered_;
    IntPtr self_handle_; // GCHandle to this (for callback user_data)
  };

} // namespace Oxygen::Editor::EngineInterface

// Implementation of the thin indirection which can now safely call into
// LogHandler because LogHandler is fully defined.
static void InvokeLogHandler(Object^ obj, const loguru::Message& msg) {
  try {
    auto handler =
      safe_cast<Oxygen::Editor::EngineInterface::LogHandler^>(obj);
    if (handler != nullptr) {
      handler->HandleLog(msg);
    }
  }
  catch (...) {
    /* swallow */
  }
}

// Token storage and resolution helpers are defined inside the
// Oxygen::Editor::EngineInterface namespace below so they can refer to
// SurfaceRegistry::GuidKey and related types without needing fully-qualified names.

// EngineRunner implementation now delegates logging concerns to LogHandler.
namespace Oxygen::Editor::EngineInterface {

  ref class SwapChainAttachState sealed {
  public:
    SwapChainAttachState(IntPtr panel, IntPtr swapChain, IntPtr surfaceHandle)
      : panel_(panel)
      , swap_chain_(swapChain)
      , surface_handle_(surfaceHandle)
    {
    }

    property IntPtr PanelPtr {
      IntPtr get() { return panel_; }
    }

    property IntPtr SwapChainPtr {
      IntPtr get() { return swap_chain_; }
    }

    property IntPtr SurfaceHandle {
      IntPtr get() { return surface_handle_; }
    }

  private:
    IntPtr panel_;
    IntPtr swap_chain_;
    IntPtr surface_handle_;
  };

  EngineRunner::EngineRunner()
    : log_handler_(nullptr)
    , disposed_(false)
    , surface_registry_(nullptr)
  {
    log_handler_ = gcnew LogHandler();
    this->ui_dispatcher_ = gcnew UiThreadDispatcher();
    this->render_thread_context_ = gcnew RenderThreadContext();
    this->engine_task_ = nullptr;
    this->engine_completion_source_ = nullptr;
    this->active_context_ = nullptr;
    this->state_lock_ = gcnew Object();
    // token map is implemented as a native map with managed gcroot values in
    // the implementation file (to allow native callbacks to resolve tokens
    // without capturing managed types in lambdas).
  }

  EngineRunner::~EngineRunner() {
    if (!disposed_) {
      EnsureEngineLoopStopped();
      ResetSurfaceRegistry();
      if (log_handler_ != nullptr) {
        delete log_handler_;
        log_handler_ = nullptr;
      }
      if (surface_registry_ != nullptr) {
        delete surface_registry_;
        surface_registry_ = nullptr;
      }
      ui_dispatcher_ = nullptr;
      render_thread_context_ = nullptr;
      disposed_ = true;
    }
  }

  EngineRunner::!EngineRunner() {
    EnsureEngineLoopStopped();
    ResetSurfaceRegistry();
    if (log_handler_ != nullptr) {
      delete log_handler_;
      log_handler_ = nullptr;
    }
    if (surface_registry_ != nullptr) {
      delete surface_registry_;
      surface_registry_ = nullptr;
    }
    ui_dispatcher_ = nullptr;
    render_thread_context_ = nullptr;
  }

  auto EngineRunner::ConfigureLogging(LoggingConfig^ config) -> bool {
    if (log_handler_ == nullptr) {
      log_handler_ = gcnew LogHandler();
    }
    return log_handler_->ConfigureLogging(config);
  }

  auto EngineRunner::ConfigureLogging(LoggingConfig^ config, Object^ logger)
    -> bool {
    if (log_handler_ == nullptr) {
      log_handler_ = gcnew LogHandler();
    }
    log_handler_->SetLogger(logger);
    return ConfigureLogging(config);
  }

  void EngineRunner::CaptureUiSynchronizationContext()
  {
    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

    auto current = SynchronizationContext::Current;
    ui_dispatcher_->Capture(current,
      gcnew String(L"CaptureUiSynchronizationContext() must be invoked on the UI thread."));
  }



  auto EngineRunner::CreateEngine(EngineConfig^ engine_cfg) -> EngineContext^ {
    return CreateEngine(engine_cfg, IntPtr::Zero);
  }

  auto EngineRunner::CreateEngine(EngineConfig^ engine_cfg, IntPtr swapChainPanel) -> EngineContext^
  {
    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

    ui_dispatcher_->CaptureCurrentOrThrow(
      gcnew String(L"CreateEngine must be invoked on the UI thread. "
        L"Call CaptureUiSynchronizationContext() before headless runs."));

    try {
      // Translate managed EngineConfig into native config.
      oxygen::EngineConfig native_cfg = engine_cfg->ToNative();

      // If we have a swap chain panel, we are in editor mode.
      // We need to configure the engine to be headless (no SDL window)
      if (swapChainPanel != IntPtr::Zero) {
        native_cfg.graphics.headless = true;
      }

      // Create the native engine context (unique ownership from factory).
      auto native_unique = oxygen::engine::interop::CreateEngine(native_cfg);
      if (!native_unique) {
        return nullptr; // creation failed
      }

      // Promote unique_ptr to shared_ptr for the managed wrapper lifetime model.
      std::shared_ptr<oxygen::engine::interop::EngineContext> shared(native_unique.release());

      EnsureSurfaceRegistry();
      auto registry = GetSurfaceRegistry();
      registry->Clear();

      if (shared->engine) {
        oxygen::engine::interop::LogInfoMessage(
          "Registering SimpleEditorModule with surface registry.");
        auto module = std::make_unique<SimpleEditorModule>(registry);
        shared->engine->RegisterModule(std::move(module));
      }

      return gcnew EngineContext(shared);
    }
    catch (const std::exception& ex) {
#if defined(_DEBUG) || !defined(NDEBUG)
      System::Diagnostics::Debug::WriteLine(gcnew System::String(ex.what()));
#endif
      return nullptr;
    }
    catch (...) {
#if defined(_DEBUG) || !defined(NDEBUG)
      System::Diagnostics::Debug::WriteLine("Unknown exception in EngineRunner::CreateEngine");
#endif
      return nullptr;
    }
  }

  auto EngineRunner::RunEngine(EngineContext^ ctx) -> void
  {
    auto task = RunEngineAsync(ctx);
    if (task == nullptr) {
      return;
    }
    task->Wait();
  }

  auto EngineRunner::RunEngineAsync(EngineContext^ ctx)
    -> Task^
  {
    if (ctx == nullptr) {
      throw gcnew ArgumentNullException("ctx");
    }
    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

    Task^ started_task = nullptr;

    Monitor::Enter(state_lock_);
    try {
      if (engine_task_ != nullptr && !engine_task_->IsCompleted) {
        throw gcnew InvalidOperationException(
          "The engine loop is already running.");
      }

      active_context_ = ctx;
      engine_completion_source_
        = gcnew TaskCompletionSource<bool>(TaskCreationOptions::RunContinuationsAsynchronously);
      engine_task_ = engine_completion_source_->Task;
      render_thread_context_->Start(
        gcnew ParameterizedThreadStart(this, &EngineRunner::EngineLoopAdapter),
        ctx, "OxygenEngineLoop");
      started_task = engine_task_;
    }
    finally {
      Monitor::Exit(state_lock_);
    }

    return started_task;
  }

  auto EngineRunner::StopEngine(EngineContext^ ctx) -> void
  {
    if (ctx == nullptr) {
      return;
    }

    // This call will signal the background engine loop to exit.
    oxygen::engine::interop::StopEngine(ctx->NativeShared());
  }

  auto EngineRunner::RegisterSurface(EngineContext^ ctx, System::Guid documentId,
    System::Guid viewportId, System::String^ displayName,
    IntPtr swapChainPanel) -> bool
  {
    if (ctx == nullptr) {
      throw gcnew ArgumentNullException("ctx");
    }
    if (swapChainPanel == IntPtr::Zero) {
      throw gcnew ArgumentException(
        "SwapChainPanel pointer must not be zero.", "swapChainPanel");
    }
    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

    ui_dispatcher_->VerifyAccess(
      gcnew String(L"RegisterSurface requires the UI thread. "
        L"Call CreateEngine() on the UI thread first."));

    auto& shared = ctx->NativeShared();
    if (!shared) {
      return false;
    }

    EnsureSurfaceRegistry();
    auto registry = GetSurfaceRegistry();
    auto key = ToGuidKey(viewportId);

    auto docString = documentId.ToString();
    auto viewportString = viewportId.ToString();
    auto displayLabel = displayName != nullptr ? displayName : gcnew String(L"(unnamed viewport)");
    const auto doc = msclr::interop::marshal_as<std::string>(docString);
    const auto view = msclr::interop::marshal_as<std::string>(viewportString);
    const auto disp = msclr::interop::marshal_as<std::string>(displayLabel);

    // Timestamped entry trace for RegisterSurface
    try {
      std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      char buf[64]{};
      std::strftime(buf, sizeof(buf), "%F %T", std::localtime(&now));
      auto registrationLog = fmt::format("[{}] RegisterSurface doc={} viewport={} name='{}'", buf, doc, view, disp);
      oxygen::engine::interop::LogInfoMessage(registrationLog.c_str());
    }
    catch (...) {
      oxygen::engine::interop::LogInfoMessage("RegisterSurface: failed to format timestamped log");
    }

    oxygen::engine::interop::LogInfoMessage("RegisterSurface: creating composition surface.");
    void* swap_chain_ptr = nullptr;
    auto surface = oxygen::engine::interop::CreateCompositionSurface(shared,
      &swap_chain_ptr);
    if (!surface) {
      oxygen::engine::interop::LogInfoMessage(
        "RegisterSurface failed: CreateCompositionSurface returned null.");
      return false;
    }

    // Set a helpful debug name that includes the document and viewport id so
    // logs identify which viewport this composition surface belongs to.
    try {
      surface->SetName(disp);
    }
    catch (...) { /* best-effort naming; ignore failures */ }

    registry->RegisterSurface(key, surface);

    // Log that registration succeeded (timestamp + diagnostic counts)
    try {
      std::time_t now2 = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      char buf2[64]{};
      std::strftime(buf2, sizeof(buf2), "%F %T", std::localtime(&now2));
      auto successLog = fmt::format("[{}] RegisterSurface completed: viewport={} swap_chain_ptr={} surface_ptr={} surface.use_count={}", buf2, view, fmt::ptr(swap_chain_ptr), fmt::ptr(surface.get()), surface.use_count());
      oxygen::engine::interop::LogInfoMessage(successLog.c_str());
    }
    catch (...) {
      oxygen::engine::interop::LogInfoMessage("RegisterSurface: failed to format success log");
    }

    if (swap_chain_ptr != nullptr) {
      // Keep a temporary owning reference to the surface so the UI attach
      // callback cannot observe a destroyed surface unexpectedly. The
      // pointer is deleted by AttachSwapChainCallback once it runs.
      auto surface_ptr = new std::shared_ptr<oxygen::graphics::Surface>(surface);
      AttachSwapChain(swapChainPanel, IntPtr(swap_chain_ptr), IntPtr(surface_ptr));
    }

    return true;
  }

  auto EngineRunner::ResizeSurface(System::Guid viewportId, System::UInt32 width,
    System::UInt32 height) -> void
  {
    if (width == 0 || height == 0) {
      return;
    }

    EnsureSurfaceRegistry();
    auto registry = GetSurfaceRegistry();
    auto key = ToGuidKey(viewportId);
    auto surface = registry->FindSurface(key);
    if (!surface) {
      return;
    }

    const auto viewportString = msclr::interop::marshal_as<std::string>(viewportId.ToString());
    auto resizeLog = fmt::format("ResizeSurface viewport={} size={}x{}", viewportString, width, height);
    oxygen::engine::interop::LogInfoMessage(resizeLog.c_str());

    // Mark the composition surface for resize; the engine module will execute
    // the actual Resize() during its next frame cycle and resolve any
    // registered tokens.
    oxygen::engine::interop::RequestCompositionSurfaceResize(surface, width,
      height);
  }

  auto EngineRunner::UnregisterSurface(System::Guid viewportId) -> void
  {
    EnsureSurfaceRegistry();
    auto registry = GetSurfaceRegistry();
    auto key = ToGuidKey(viewportId);
    const auto viewportString = msclr::interop::marshal_as<std::string>(viewportId.ToString());
    try {
      std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      char buf[64]{};
      std::strftime(buf, sizeof(buf), "%F %T", std::localtime(&now));
      auto unregisterLog = fmt::format("[{}] UnregisterSurface viewport={}", buf, viewportString);
      oxygen::engine::interop::LogInfoMessage(unregisterLog.c_str());
    }
    catch (...) {
      oxygen::engine::interop::LogInfoMessage("UnregisterSurface: failed to format timestamped log");
    }
    // Stage removal into the registry; do not final-release on the UI thread.
    registry->RemoveSurface(key);
  }

  auto EngineRunner::UnregisterSurfaceAsync(System::Guid viewportId) -> Task<bool>^
  {
    EnsureSurfaceRegistry();
    auto registry = GetSurfaceRegistry();
    auto key = ToGuidKey(viewportId);

    // Create the TaskCompletionSource and store it so we can resolve it when
    // the engine module processes the pending destruction.
    auto tcs = gcnew TaskCompletionSource<bool>(TaskCreationOptions::RunContinuationsAsynchronously);

    // Store the TaskCompletionSource in the native tokens_map keyed by the
    // native GuidKey (array of bytes). Use tokens_mutex for thread safety.
    TokenKey nativeKey;
    for (size_t i = 0; i < nativeKey.size(); ++i) nativeKey[i] = key[i];

    // Create a native callback that resolves the stored TaskCompletionSource
    // when the engine module processes the pending destruction. Use the
    // MakeResolveCallback helper (outside the managed member function) so
    // no local class is defined inside this managed method.
    std::function<void(bool)> cb = MakeResolveCallback(nativeKey);

    try {
      auto msg = fmt::format("UnregisterSurfaceAsync: stored token for viewport={}", msclr::interop::marshal_as<std::string>(viewportId.ToString()));
      oxygen::engine::interop::LogInfoMessage(msg.c_str());
    }
    catch (...) { /* swallow logging errors */ }

    // Pin the managed TaskCompletionSource using a GCHandle and store the
    // IntPtr -> pointer value in the native map so callbacks can resolve it
    // without holding managed references. Keep hold of the IntPtr so we can
    // free it if staging into the registry fails.
    System::IntPtr ip = IntPtr::Zero;
    {
      auto gh = System::Runtime::InteropServices::GCHandle::Alloc(tcs, System::Runtime::InteropServices::GCHandleType::Normal);
      ip = System::Runtime::InteropServices::GCHandle::ToIntPtr(gh);
      std::lock_guard<std::mutex> lg(tokens_mutex);
      tokens_map[nativeKey] = ip.ToPointer();
    }

    try {
      auto msg = fmt::format("UnregisterSurfaceAsync: stored token for viewport={}", msclr::interop::marshal_as<std::string>(viewportId.ToString()));
      oxygen::engine::interop::LogInfoMessage(msg.c_str());
    }
    catch (...) { /* swallow logging errors */ }

    // Stage the removal into the registry; callback will be invoked by the
    // engine module when it drains pending destructions. If staging fails we
    // must cleanup the pinned GCHandle and remove the entry from tokens_map
    // to avoid leaking.
    try {
      registry->RemoveSurface(key, std::move(cb));
      try {
        auto msg2 = fmt::format("UnregisterSurfaceAsync: staged removal for viewport={}", msclr::interop::marshal_as<std::string>(viewportId.ToString()));
        oxygen::engine::interop::LogInfoMessage(msg2.c_str());
      }
      catch (...) { /* ignore logging failures */ }
    }
    catch (...) {
      // ensure the saved GCHandle is freed and token removed
      try {
        auto msg = fmt::format("UnregisterSurfaceAsync: staging removal failed for viewport={}, cleaning up token.", msclr::interop::marshal_as<std::string>(viewportId.ToString()));
        oxygen::engine::interop::LogInfoMessage(msg.c_str());
      }
      catch (...) { /* swallow */ }
      std::lock_guard<std::mutex> lg(tokens_mutex);
      auto it = tokens_map.find(nativeKey);
      if (it != tokens_map.end()) {
        void* hv = it->second;
        if (hv != nullptr) {
          try {
            System::IntPtr stored(hv);
            auto gh = System::Runtime::InteropServices::GCHandle::FromIntPtr(stored);
            if (gh.IsAllocated) gh.Free();
          }
          catch (...) { /* swallow */ }
        }
        tokens_map.erase(it);
      }

      // Fail the TaskCompletionSource so the caller does not hang
      try { tcs->TrySetResult(false); } catch (...) { /* swallow */ }

      return tcs->Task;
    }

    return tcs->Task;
  }

  auto EngineRunner::ResizeSurfaceAsync(System::Guid viewportId, System::UInt32 width,
    System::UInt32 height) -> Task<bool>^
  {
    if (width == 0 || height == 0) {
      return Task::FromResult<bool>(false);
    }

    EnsureSurfaceRegistry();
    auto registry = GetSurfaceRegistry();
    auto key = ToGuidKey(viewportId);
    auto surface = registry->FindSurface(key);
    if (!surface) {
      return Task::FromResult<bool>(false);
    }


    auto tcs = gcnew TaskCompletionSource<bool>(TaskCreationOptions::RunContinuationsAsynchronously);
    TokenKey nativeKey;
    for (size_t i = 0; i < nativeKey.size(); ++i) nativeKey[i] = key[i];
    System::IntPtr ip = IntPtr::Zero;
    {
      auto gh = System::Runtime::InteropServices::GCHandle::Alloc(tcs, System::Runtime::InteropServices::GCHandleType::Normal);
      ip = System::Runtime::InteropServices::GCHandle::ToIntPtr(gh);
      std::lock_guard<std::mutex> lg(tokens_mutex);
      tokens_map[nativeKey] = ip.ToPointer();
    }

    std::function<void(bool)> cb = MakeResolveCallback(nativeKey);

    try {
      registry->RegisterResizeCallback(key, std::move(cb));
      try {
        auto msg = fmt::format("ResizeSurfaceAsync: staged resize for viewport={} size={}x{}",
          msclr::interop::marshal_as<std::string>(viewportId.ToString()), width, height);
        oxygen::engine::interop::LogInfoMessage(msg.c_str());
      }
      catch (...) { /* swallow */ }
    }
    catch (...) {
      // cleanup pinned handle + native entry if registration fails
      std::lock_guard<std::mutex> lg(tokens_mutex);
      auto it = tokens_map.find(nativeKey);
      if (it != tokens_map.end()) {
        void* hv = it->second;
        if (hv != nullptr) {
          try {
            System::IntPtr stored(hv);
            auto gh = System::Runtime::InteropServices::GCHandle::FromIntPtr(stored);
            if (gh.IsAllocated) gh.Free();
          }
          catch (...) { /* swallow */ }
        }
        tokens_map.erase(it);
      }

      try { tcs->TrySetResult(false); } catch (...) { /* swallow */ }
      return tcs->Task;
    }

    // Request the resize (mark-only). Engine module will pick this up and
    // perform the actual Resize() on next frame.
    oxygen::engine::interop::RequestCompositionSurfaceResize(surface, width, height);

    return tcs->Task;
  }

  void EngineRunner::EngineLoopAdapter(System::Object^ state)
  {
    auto ctx = safe_cast<EngineContext^>(state);
    TaskCompletionSource<bool>^ completion = nullptr;

    Monitor::Enter(state_lock_);
    try {
      completion = engine_completion_source_;
    }
    finally {
      Monitor::Exit(state_lock_);
    }

    try {
      try {
        auto startMsg = fmt::format("EngineLoopAdapter: starting engine loop for ctx_ptr={}", fmt::ptr(ctx->NativeShared().get()));
        oxygen::engine::interop::LogInfoMessage(startMsg.c_str());
      }
      catch (...) { /* swallow logging failures */ }

      oxygen::engine::interop::RunEngine(ctx->NativeShared());

      try {
        auto endMsg = fmt::format("EngineLoopAdapter: engine loop finished for ctx_ptr={}", fmt::ptr(ctx->NativeShared().get()));
        oxygen::engine::interop::LogInfoMessage(endMsg.c_str());
      }
      catch (...) { /* swallow logging failures */ }
      if (completion != nullptr) {
        completion->TrySetResult(true);
      }
    }
    catch (const std::exception& ex) {
      auto message = gcnew String(ex.what());
#if defined(_DEBUG) || !defined(NDEBUG)
      Debug::WriteLine(message);
#endif
      if (completion != nullptr) {
        completion->TrySetException(gcnew InvalidOperationException(message));
      }
    }
    catch (...) {
#if defined(_DEBUG) || !defined(NDEBUG)
      Debug::WriteLine("Unknown exception in EngineRunner::EngineLoopAdapter");
#endif
      if (completion != nullptr) {
        completion->TrySetException(gcnew InvalidOperationException(
          "Engine loop terminated due to an unknown native exception."));
      }
    }
    finally {
      DispatchToUi(gcnew Action(this, &EngineRunner::OnEngineLoopExited));
    }
  }

  void EngineRunner::OnEngineLoopExited()
  {
    oxygen::engine::interop::LogInfoMessage(
      "OnEngineLoopExited invoked; clearing surface registry.");
    ResetSurfaceRegistry();

    // No managed pending_tokens_ map is defined in this class; we use the
    // native tokens_map for outstanding tokens. Proceed to clear native
    // outstanding entries instead (done below).

    // Also fail any outstanding native tokens_map entries so awaiting callers
    // using the async native APIs do not hang when the engine loop exits.
    {
      std::lock_guard<std::mutex> lg(tokens_mutex);
      try {
        auto msg = fmt::format("OnEngineLoopExited: failing outstanding tokens_map entries (count={})", tokens_map.size());
        oxygen::engine::interop::LogInfoMessage(msg.c_str());
      }
      catch (...) { /* ignore logging failures */ }

      for (auto &it : tokens_map) {
        void* hv = it.second;
        if (hv != nullptr) {
          try {
            System::IntPtr ip(hv);
            auto gh = System::Runtime::InteropServices::GCHandle::FromIntPtr(ip);
            auto tcs = safe_cast<System::Threading::Tasks::TaskCompletionSource<bool>^>(gh.Target);
            if (tcs != nullptr) {
              tcs->TrySetResult(false);
            }
            gh.Free();
          }
          catch (...) { /* swallow */ }
        }
      }
      tokens_map.clear();
    }

    Monitor::Enter(state_lock_);
    try {
      engine_task_ = nullptr;
      active_context_ = nullptr;
      engine_completion_source_ = nullptr;
      if (render_thread_context_ != nullptr) {
        render_thread_context_->Clear();
      }
    }
    finally {
      Monitor::Exit(state_lock_);
    }
  }

  void EngineRunner::DispatchToUi(Action^ action)
  {
    if (action == nullptr) {
      return;
    }

    if (ui_dispatcher_ != nullptr && ui_dispatcher_->IsCaptured) {
      ui_dispatcher_->Post(
        gcnew SendOrPostCallback(this, &EngineRunner::InvokeAction), action);
      return;
    }

    action();
  }

  void EngineRunner::DispatchToUi(SendOrPostCallback^ callback, Object^ state)
  {
    if (callback == nullptr) {
      return;
    }

    if (ui_dispatcher_ != nullptr && ui_dispatcher_->IsCaptured) {
      ui_dispatcher_->Post(callback, state);
      return;
    }

    callback->Invoke(state);
  }

  void EngineRunner::SendToUi(SendOrPostCallback^ callback, Object^ state)
  {
    if (callback == nullptr) {
      return;
    }

    if (ui_dispatcher_ != nullptr && ui_dispatcher_->IsCaptured) {
      ui_dispatcher_->Send(callback, state);
      return;
    }

    callback->Invoke(state);
  }

  void EngineRunner::InvokeAction(Object^ action)
  {
    auto callback = dynamic_cast<Action^>(action);
    if (callback != nullptr) {
      callback();
    }
  }

  void EngineRunner::ResetSurfaceRegistry()
  {
    if (surface_registry_ != nullptr && surface_registry_->get() != nullptr) {
      (*surface_registry_)->Clear();
    }
  }

  void EngineRunner::EnsureSurfaceRegistry()
  {
    if (surface_registry_ == nullptr) {
      surface_registry_ =
        new std::shared_ptr<SurfaceRegistry>(std::make_shared<SurfaceRegistry>());
      return;
    }

    if (surface_registry_->get() == nullptr) {
      *surface_registry_ = std::make_shared<SurfaceRegistry>();
    }
  }

  auto EngineRunner::GetSurfaceRegistry() -> std::shared_ptr<SurfaceRegistry>
  {
    EnsureSurfaceRegistry();
    return *surface_registry_;
  }

  auto EngineRunner::ToGuidKey(System::Guid guid) -> SurfaceRegistry::GuidKey
  {
    SurfaceRegistry::GuidKey key{};
    auto bytes = guid.ToByteArray();
    if (bytes == nullptr || bytes->Length != 16) {
      return key;
    }

    for (int i = 0; i < 16; ++i) {
      key[static_cast<std::size_t>(i)] = bytes[i];
    }

    return key;
  }

  void EngineRunner::AttachSwapChain(IntPtr panelPtr, IntPtr swapChainPtr, IntPtr surfaceHandle)
  {
    if (panelPtr == IntPtr::Zero || swapChainPtr == IntPtr::Zero) {
      return;
    }

    auto state = gcnew SwapChainAttachState(panelPtr, swapChainPtr, surfaceHandle);
    if (ui_dispatcher_ == nullptr || !ui_dispatcher_->IsCaptured) {
      throw gcnew InvalidOperationException(gcnew String(
        L"SwapChain attachment requires a captured UI SynchronizationContext. "
        L"Ensure CreateEngine() was called on the UI thread."));
    }

    ui_dispatcher_->Post(
      gcnew SendOrPostCallback(this, &EngineRunner::AttachSwapChainCallback), state);
  }

  void EngineRunner::AttachSwapChainCallback(Object^ state)
  {
    auto attachState = dynamic_cast<SwapChainAttachState^>(state);
    if (attachState == nullptr) {
      return;
    }

    auto panelUnknown = reinterpret_cast<IUnknown*>(attachState->PanelPtr.ToPointer());
    auto swapChain = reinterpret_cast<IDXGISwapChain*>(attachState->SwapChainPtr.ToPointer());
    auto surfaceHandlePtr = reinterpret_cast<std::shared_ptr<oxygen::graphics::Surface>*>(attachState->SurfaceHandle.ToPointer());
    // Log the incoming attach with timestamp and surface reference info (if provided).
    try {
      std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      char buf[64]{};
      std::strftime(buf, sizeof(buf), "%F %T", std::localtime(&now));
      auto attachLog = fmt::format("[{}] AttachSwapChainCallback: panel={} swapchain={}", buf, fmt::ptr(panelUnknown), fmt::ptr(swapChain));
      if (surfaceHandlePtr != nullptr) {
        attachLog += fmt::format(" surface_handle_ptr={} use_count={}", fmt::ptr(surfaceHandlePtr), surfaceHandlePtr->use_count());
      }
      oxygen::engine::interop::LogInfoMessage(attachLog.c_str());
    }
    catch (...) {
      oxygen::engine::interop::LogInfoMessage("AttachSwapChainCallback: failed to format attach diagnostics.");
    }
    if (panelUnknown == nullptr || swapChain == nullptr) {
      return;
    }

    ISwapChainPanelNative* panelNative = nullptr;
    HRESULT hr = panelUnknown->QueryInterface(__uuidof(ISwapChainPanelNative), reinterpret_cast<void**>(&panelNative));
    if (FAILED(hr) || panelNative == nullptr) {
      oxygen::engine::interop::LogInfoMessage("Failed to acquire ISwapChainPanelNative from SwapChainPanel.");
      return;
    }

    hr = panelNative->SetSwapChain(swapChain);
    panelNative->Release();
    if (FAILED(hr)) {
      oxygen::engine::interop::LogInfoMessage("ISwapChainPanelNative::SetSwapChain failed.");
      // cleanup surface handle if present
      if (surfaceHandlePtr != nullptr) {
        try {
          auto errLog = fmt::format("AttachSwapChainCallback: SetSwapChain failed, cleaning surface_handle_ptr={} pre-delete use_count={}", fmt::ptr(surfaceHandlePtr), surfaceHandlePtr->use_count());
          oxygen::engine::interop::LogInfoMessage(errLog.c_str());
        }
        catch (...) {
          oxygen::engine::interop::LogInfoMessage("AttachSwapChainCallback: error logging before delete.");
        }
        delete surfaceHandlePtr;
      }
      return;
    }

    oxygen::engine::interop::LogInfoMessage("SwapChain attached to panel.");

    // if we received a temporary owning pointer, drop it now to return ownership
    // to the registry/engine. We intentionally log the use_count for diagnostics
    // before deleting the heap-held shared_ptr.
    if (surfaceHandlePtr != nullptr) {
      try {
        auto cleanupLog = fmt::format("AttachSwapChainCallback cleaning surface_handle_ptr={} pre-delete use_count={}", fmt::ptr(surfaceHandlePtr), surfaceHandlePtr->use_count());
        oxygen::engine::interop::LogInfoMessage(cleanupLog.c_str());
      }
      catch (...) {
        oxygen::engine::interop::LogInfoMessage("AttachSwapChainCallback: error logging cleanup info.");
      }
      delete surfaceHandlePtr;
    }
  }

  void EngineRunner::EnsureEngineLoopStopped()
  {
    Task^ running_task = nullptr;
    EngineContext^ ctx = nullptr;

    Monitor::Enter(state_lock_);
    try {
      running_task = engine_task_;
      ctx = active_context_;
    }
    finally {
      Monitor::Exit(state_lock_);
    }

    if (running_task != nullptr && !running_task->IsCompleted) {
      if (ctx != nullptr) {
        try {
          oxygen::engine::interop::StopEngine(ctx->NativeShared());
        }
        catch (...) {
          // Swallow exceptions during shutdown to avoid tearing down the process.
        }
      }

      try {
        running_task->Wait();
      }
      catch (...) {
        // Ignore exceptions when waiting for shutdown from the finalizer path.
      }

      if (render_thread_context_ != nullptr) {
        render_thread_context_->Join();
        render_thread_context_->Clear();
      }

      DispatchToUi(gcnew Action(this, &EngineRunner::ResetSurfaceRegistry));

      Monitor::Enter(state_lock_);
      try {
        engine_task_ = nullptr;
        engine_completion_source_ = nullptr;
        active_context_ = nullptr;
      }
      finally {
        Monitor::Exit(state_lock_);
      }
    }
  }

} // namespace Oxygen::Editor::EngineInterface
