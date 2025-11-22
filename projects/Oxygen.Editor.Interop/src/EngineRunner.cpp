//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma managed

#include "EngineRunner.h"
#include "SimpleEditorModule.h"

#include <msclr/marshal.h>
#include <msclr/marshal_cppstd.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/EditorInterface/Api.h>

#include <dxgi1_2.h>
#include <sstream>

// WinUI 3 ISwapChainPanelNative definition (desktop IID)
struct __declspec(uuid("63AAD0B8-7C24-40FF-85A8-640D944CC325")) ISwapChainPanelNative : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetSwapChain(IDXGISwapChain *swapChain) = 0;
};

using namespace System;
using namespace System::Diagnostics;
using namespace System::Threading;
using namespace System::Threading::Tasks;
using namespace Microsoft::Extensions::Logging;

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

  // Managed helper that encapsulates all logging-related state and behavior so
  // the EngineRunner header doesn't need to include or reference native logging
  // internals.
  public
    ref class LogHandler sealed {
    public:
      LogHandler()
        : logger_(nullptr),
        callback_registered_(false), self_handle_(IntPtr::Zero) {
        Runtime::InteropServices::GCHandle h =
          Runtime::InteropServices::GCHandle::Alloc(
            this,
            Runtime::InteropServices::GCHandleType::WeakTrackResurrection);
        self_handle_ = Runtime::InteropServices::GCHandle::ToIntPtr(h);
      }

      ~LogHandler() { ReleaseCallback(); }

      !LogHandler() { ReleaseCallback(); }

      void SetLogger(Object^ logger) {
        if (logger == nullptr) {
            logger_ = nullptr;
            return;
        }
        // Cast to ILogger directly.
        logger_ = dynamic_cast<ILogger^>(logger);
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
        }
        // Log an INFO message, used for testing and also nice to have for the
        // status.
        op::LogInfoMessage("Oxygen Editor logging configured.");
        // Flush so we can see that message right away.
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

          // Use the extension method or direct interface call?
          // ILogger::Log is generic.
          // void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception^ exception, Func<TState, Exception^, String^>^ formatter);

          // We can use a simple formatter lambda or delegate.
          // But C++/CLI lambdas to delegates are tricky.
          // Let's use a static method for formatter.

          logger_->Log<String^>(lvlValue, EventId(0), managedMsg, nullptr, gcnew Func<String^, Exception^, String^>(&LogHandler::Format));
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

namespace {

auto FormatSurfaceState(const char* prefix,
  const std::shared_ptr<oxygen::graphics::Surface>* surface_ptr)
  -> std::string
{
  std::ostringstream oss;
  oss << prefix;
  if (surface_ptr == nullptr) {
    oss << " editor_surface_=nullptr";
    return oss.str();
  }

  oss << " wrapper=" << surface_ptr;
  if (surface_ptr->get() == nullptr && surface_ptr->use_count() == 0) {
    oss << " (shared_ptr reset)";
    return oss.str();
  }

  oss << " shared_ptr=" << surface_ptr->get();
  oss << " use_count=" << surface_ptr->use_count();
  return oss.str();
}

void LogSurfaceState(const char* prefix,
  const std::shared_ptr<oxygen::graphics::Surface>* surface_ptr)
{
  auto message = FormatSurfaceState(prefix, surface_ptr);
  oxygen::engine::interop::LogInfoMessage(message.c_str());
}

}
// EngineRunner implementation now delegates logging concerns to LogHandler.
namespace Oxygen::Editor::EngineInterface {

  ref class SwapChainAttachState sealed {
  public:
    SwapChainAttachState(IntPtr panel, IntPtr swapChain)
      : panel_(panel)
      , swap_chain_(swapChain)
    {
    }

    property IntPtr PanelPtr {
      IntPtr get() { return panel_; }
    }

    property IntPtr SwapChainPtr {
      IntPtr get() { return swap_chain_; }
    }

  private:
    IntPtr panel_;
    IntPtr swap_chain_;
  };

  EngineRunner::EngineRunner()
    : log_handler_(nullptr)
    , disposed_(false)
    , editor_surface_(nullptr)
  {
    log_handler_ = gcnew LogHandler();
    this->engine_task_ = nullptr;
    this->engine_completion_source_ = nullptr;
    this->engine_thread_ = nullptr;
    this->ui_sync_context_ = nullptr;
    this->active_context_ = nullptr;
    this->state_lock_ = gcnew Object();
  }

  EngineRunner::~EngineRunner() {
    if (!disposed_) {
      EnsureEngineLoopStopped();
      if (log_handler_ != nullptr) {
        delete log_handler_;
        log_handler_ = nullptr;
      }
      if (editor_surface_ != nullptr) {
        delete editor_surface_;
        editor_surface_ = nullptr;
      }
      disposed_ = true;
    }
  }

  EngineRunner::!EngineRunner() {
    EnsureEngineLoopStopped();
    if (log_handler_ != nullptr) {
      delete log_handler_;
      log_handler_ = nullptr;
    }
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
    auto current = SynchronizationContext::Current;
    if (current != nullptr) {
      ui_sync_context_ = current;
    }
  }



  auto EngineRunner::CreateEngine(EngineConfig^ engine_cfg) -> EngineContext^ {
    return CreateEngine(engine_cfg, IntPtr::Zero);
  }

  auto EngineRunner::CreateEngine(EngineConfig^ engine_cfg, IntPtr swapChainPanel) -> EngineContext^
  {
    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

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

      if (swapChainPanel != IntPtr::Zero) {
        void* native_panel = swapChainPanel.ToPointer();

        oxygen::engine::interop::LogInfoMessage("Creating CompositionSurface...");

        void* swap_chain_ptr = nullptr;
        // Create Surface
        auto surface = oxygen::engine::interop::CreateCompositionSurface(shared,
          &swap_chain_ptr);
        if (surface) {
          ResetEditorSurface();
          editor_surface_ = new std::shared_ptr<oxygen::graphics::Surface>(surface);
          LogSurfaceState("CreateEngine stored editor surface", editor_surface_);
            // Ensure we delete old weak_ptr before assigning new, and delete only once.
            if (swap_chain_ptr) {
             oxygen::engine::interop::LogInfoMessage("CompositionSurface created. Setting SwapChain on Panel...");
             AttachSwapChain(IntPtr(native_panel), IntPtr(swap_chain_ptr));
          } else {
              oxygen::engine::interop::LogInfoMessage("WARNING: CompositionSurface created but no SwapChain returned.");
          }

          oxygen::engine::interop::LogInfoMessage("Creating SimpleEditorModule...");
          // Create Module
          auto module = std::make_unique<SimpleEditorModule>(surface);

          // Register Module
          if (shared->engine) {
            oxygen::engine::interop::LogInfoMessage("Registering SimpleEditorModule...");
            shared->engine->RegisterModule(std::move(module));
            oxygen::engine::interop::LogInfoMessage("SimpleEditorModule registered.");
          }
        } else {
            oxygen::engine::interop::LogInfoMessage("ERROR: Failed to create CompositionSurface.");
            ResetEditorSurface();
        }
      }
      else if (editor_surface_ != nullptr) {
        ResetEditorSurface();
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

    auto sync_context = SynchronizationContext::Current;
    if (sync_context != nullptr) {
      ui_sync_context_ = sync_context;
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
      engine_thread_
        = gcnew Thread(gcnew ParameterizedThreadStart(this, &EngineRunner::EngineLoopAdapter));
      engine_thread_->IsBackground = true;
      engine_thread_->Name = "OxygenEngineLoop";
      engine_thread_->Start(ctx);
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

  auto EngineRunner::ResizeViewport(System::UInt32 width, System::UInt32 height)
    -> void
  {
    std::ostringstream oss;
    oss << "ResizeViewport requested " << width << "x" << height;
    oxygen::engine::interop::LogInfoMessage(oss.str().c_str());

    if (width == 0 || height == 0) {
      oxygen::engine::interop::LogInfoMessage(
        "ResizeViewport ignored: zero dimension request.");
      return;
    }

    if (editor_surface_ == nullptr) {
      oxygen::engine::interop::LogInfoMessage(
        "ResizeViewport ignored: editor_surface_ wrapper missing.");
      return;
    }

    auto surface = *editor_surface_;
    if (!surface) {
      oxygen::engine::interop::LogInfoMessage(
        "ResizeViewport ignored: shared_ptr not set.");
      return;
    }

    LogSurfaceState("ResizeViewport issuing native resize", editor_surface_);
    oxygen::engine::interop::RequestCompositionSurfaceResize(surface, width,
      height);
  }

  void EngineRunner::ReleaseEditorSurface()
  {
    oxygen::engine::interop::LogInfoMessage(
      "ReleaseEditorSurface invoked explicitly.");
    ResetEditorSurface();
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
      oxygen::engine::interop::RunEngine(ctx->NativeShared());
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
      "OnEngineLoopExited invoked; resetting editor surface.");
    ResetEditorSurface();

    Monitor::Enter(state_lock_);
    try {
      engine_task_ = nullptr;
      active_context_ = nullptr;
      engine_thread_ = nullptr;
      engine_completion_source_ = nullptr;
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

    auto ctx = ui_sync_context_;
    if (ctx != nullptr) {
      ctx->Post(gcnew SendOrPostCallback(this, &EngineRunner::InvokeAction),
        action);
      return;
    }

    action();
  }

  void EngineRunner::DispatchToUi(SendOrPostCallback^ callback, Object^ state)
  {
    if (callback == nullptr) {
      return;
    }

    auto ctx = ui_sync_context_;
    if (ctx != nullptr) {
      ctx->Post(callback, state);
      return;
    }

    callback->Invoke(state);
  }

  void EngineRunner::SendToUi(SendOrPostCallback^ callback, Object^ state)
  {
    if (callback == nullptr) {
      return;
    }

    auto ctx = ui_sync_context_;
    if (ctx != nullptr) {
      ctx->Send(callback, state);
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

  void EngineRunner::AttachSwapChain(IntPtr panelPtr, IntPtr swapChainPtr)
  {
    if (panelPtr == IntPtr::Zero || swapChainPtr == IntPtr::Zero) {
      return;
    }

    auto state = gcnew SwapChainAttachState(panelPtr, swapChainPtr);
    DispatchToUi(gcnew SendOrPostCallback(this, &EngineRunner::AttachSwapChainCallback), state);
  }

  void EngineRunner::AttachSwapChainCallback(Object^ state)
  {
    auto attachState = dynamic_cast<SwapChainAttachState^>(state);
    if (attachState == nullptr) {
      return;
    }

    auto panelUnknown = reinterpret_cast<IUnknown*>(attachState->PanelPtr.ToPointer());
    auto swapChain = reinterpret_cast<IDXGISwapChain*>(attachState->SwapChainPtr.ToPointer());
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
      return;
    }

    oxygen::engine::interop::LogInfoMessage("SwapChain attached to panel.");
  }

  void EngineRunner::ResetEditorSurface()
  {
    LogSurfaceState("ResetEditorSurface begin", editor_surface_);
    if (editor_surface_ != nullptr) {
      delete editor_surface_;
      editor_surface_ = nullptr;
    }
    LogSurfaceState("ResetEditorSurface end", editor_surface_);
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

      DispatchToUi(gcnew Action(this, &EngineRunner::ResetEditorSurface));

      Monitor::Enter(state_lock_);
      try {
        engine_task_ = nullptr;
        engine_thread_ = nullptr;
        engine_completion_source_ = nullptr;
        active_context_ = nullptr;
      }
      finally {
        Monitor::Exit(state_lock_);
      }
    }
  }

} // namespace Oxygen::Editor::EngineInterface
