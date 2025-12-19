//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma managed

#include "pch.h"

#include <EditorModule/EditorModule.h>
#include <EngineRunner.h>
#include <LogHandler.h>
#include <Utils/TokenHelpers.h>

// WinUI 3 ISwapChainPanelNative definition (desktop IID)
struct __declspec(uuid("63AAD0B8-7C24-40FF-85A8-640D944CC325"))
  ISwapChainPanelNative : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE SetSwapChain(IDXGISwapChain* swapChain) = 0;
};

using namespace System;
using namespace System::Diagnostics;
using namespace System::Threading;
using namespace System::Threading::Tasks;
using namespace Microsoft::Extensions::Logging;
using namespace oxygen::interop::module;

using oxygen::engine::interop::LogInfoMessage;

namespace Oxygen::Interop {

  EngineRunner::EngineRunner()
    : log_handler_(nullptr), disposed_(false), surface_registry_(nullptr) {
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

  auto EngineRunner::CreateEngine(EngineConfig^ engine_cfg) -> EngineContext^ {
    return CreateEngine(engine_cfg, IntPtr::Zero);
  }

  auto EngineRunner::CreateEngine(EngineConfig^ engine_cfg,
    IntPtr swapChainPanel) -> EngineContext
    ^ {
    using namespace oxygen::graphics;
    using namespace oxygen::engine;

    if (disposed_) {
      throw gcnew ObjectDisposedException("EngineRunner");
    }

    ui_dispatcher_->CaptureCurrent(gcnew String(L"CreateEngine"));

    try {
      // Translate managed EngineConfig into native config.
      oxygen::EngineConfig native_cfg = engine_cfg->ToNative();

      // If we have a swap chain panel, we are in editor mode.
      // We need to configure the engine to be headless (no SDL window)
      if (swapChainPanel != IntPtr::Zero) {
        native_cfg.graphics.headless = true;
      }

      // Create the native engine context (unique ownership from factory).
      auto native_unique = interop::CreateEngine(native_cfg);
      if (!native_unique) {
        return nullptr; // creation failed
      }

      // Promote unique_ptr to shared_ptr for the managed wrapper lifetime model.
      std::shared_ptr<interop::EngineContext> shared(native_unique.release());

      EnsureSurfaceRegistry();
      auto registry = GetSurfaceRegistry();
      registry->Clear();

      if (shared->engine) {
        interop::LogInfoMessage(
          "Registering renderer and EditorModule with surface registry.");

        // Create the renderer module and register it with the engine.
        // Required by the EditorModule.
        oxygen::RendererConfig renderer_config{
            .upload_queue_key =
                shared->queue_strategy.KeyFor(QueueRole::kTransfer).get(),
        };
        auto renderer_unique =
          std::make_unique<Renderer>(shared->gfx_weak, renderer_config);
        // Store observer ptr in the EngineContext so managed code can access it
        shared->renderer = oxygen::observer_ptr<Renderer>(renderer_unique.get());
        shared->engine->RegisterModule(std::move(renderer_unique));

        // Register the Editor module (requires surface registry)
        auto module = std::make_unique<EditorModule>(registry);
        shared->engine->RegisterModule(std::move(module));
      }

      return gcnew EngineContext(shared);
    }
    catch (const std::exception& ex) {
#if defined(_DEBUG) ||                                                         \
    !defined(NDEBUG) // FIXME: use proper logging with LoguruWrapper
      ::System::Diagnostics::Debug::WriteLine(gcnew::System::String(ex.what()));
#endif
      return nullptr;
    }
    catch (...) {
#if defined(_DEBUG) ||                                                         \
    !defined(NDEBUG) // FIXME: use proper logging with LoguruWrapper
      ::System::Diagnostics::Debug::WriteLine(
        "Unknown exception in EngineRunner::CreateEngine");
#endif
      return nullptr;
    }
  }

  auto EngineRunner::RunEngine(EngineContext^ ctx) -> void {
    auto task = RunEngineAsync(ctx);
    if (task == nullptr) {
      return;
    }
    task->Wait();
  }

  auto EngineRunner::RunEngineAsync(EngineContext^ ctx) -> Task^ {
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
      engine_completion_source_ = gcnew TaskCompletionSource<bool>(
        TaskCreationOptions::RunContinuationsAsynchronously);
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

  auto EngineRunner::StopEngine(EngineContext^ ctx) -> void {
    if (ctx == nullptr) {
      return;
    }

    // This call will signal the background engine loop to exit.
    oxygen::engine::interop::StopEngine(ctx->NativeShared());
  }

  auto EngineRunner::SetTargetFps(EngineContext^ ctx, System::UInt32 fps)
    -> void {
    if (ctx == nullptr) {
      return;
    }

    oxygen::engine::interop::SetTargetFps(ctx->NativeShared(),
      static_cast<uint32_t>(fps));
  }

  auto EngineRunner::GetEngineConfig(EngineContext^ ctx) -> EngineConfig^ {
    if (ctx == nullptr) {
      throw gcnew ArgumentNullException("ctx");
    }
    auto native_cfg =
      oxygen::engine::interop::GetEngineConfig(ctx->NativeShared());
    return EngineConfig::FromNative(native_cfg);
  }

  void EngineRunner::EngineLoopAdapter(System::Object^ state) {
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
        auto startMsg =
          fmt::format("EngineLoopAdapter: starting engine loop for ctx_ptr={}",
            fmt::ptr(ctx->NativeShared().get()));
        LogInfoMessage(startMsg.c_str());
      }
      catch (...) { /* swallow logging failures */
      }

      oxygen::engine::interop::RunEngine(ctx->NativeShared());

      try {
        auto endMsg =
          fmt::format("EngineLoopAdapter: engine loop finished for ctx_ptr={}",
            fmt::ptr(ctx->NativeShared().get()));
        LogInfoMessage(endMsg.c_str());
      }
      catch (...) { /* swallow logging failures */
      }
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

  void EngineRunner::OnEngineLoopExited() {
    LogInfoMessage("OnEngineLoopExited invoked; clearing surface registry.");
    ResetSurfaceRegistry();

    // No managed pending_tokens_ map is defined in this class; we use the
    // native tokens_map for outstanding tokens. Proceed to clear native
    // outstanding entries instead (done below).

    // Also fail any outstanding native tokens_map entries so awaiting callers
    // using the async native APIs do not hang when the engine loop exits.
    {
      std::lock_guard<std::mutex> lg(tokens_mutex);
      try {
        auto msg = fmt::format("OnEngineLoopExited: failing outstanding "
          "tokens_map entries (count={})",
          tokens_map.size());
        LogInfoMessage(msg.c_str());
      }
      catch (...) { /* ignore logging failures */
      }

      for (auto& it : tokens_map) {
        void* hv = it.second;
        if (hv != nullptr) {
          try {
            System::IntPtr ip(hv);
            auto gh = System::Runtime::InteropServices::GCHandle::FromIntPtr(ip);
            auto tcs =
              safe_cast<System::Threading::Tasks::TaskCompletionSource<bool>^>(
                gh.Target);
            if (tcs != nullptr) {
              tcs->TrySetResult(false);
            }
            gh.Free();
          }
          catch (...) { /* swallow */
          }
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

  void EngineRunner::DispatchToUi(Action^ action) {
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

  void EngineRunner::DispatchToUi(SendOrPostCallback^ callback, Object^ state) {
    if (callback == nullptr) {
      return;
    }

    if (ui_dispatcher_ != nullptr && ui_dispatcher_->IsCaptured) {
      ui_dispatcher_->Post(callback, state);
      return;
    }

    callback->Invoke(state);
  }

  void EngineRunner::SendToUi(SendOrPostCallback^ callback, Object^ state) {
    if (callback == nullptr) {
      return;
    }

    if (ui_dispatcher_ != nullptr && ui_dispatcher_->IsCaptured) {
      ui_dispatcher_->Send(callback, state);
      return;
    }

    callback->Invoke(state);
  }

  void EngineRunner::InvokeAction(Object^ action) {
    auto callback = dynamic_cast<Action^>(action);
    if (callback != nullptr) {
      callback();
    }
  }

  void EngineRunner::EnsureEngineLoopStopped() {
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

} // namespace Oxygen::Interop
