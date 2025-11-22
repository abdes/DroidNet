//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed

#include <memory>

#include "Config.h"
#include "EngineContext.h"
#include "RenderThreadContext.h"
#include "SurfaceRegistry.h"
#include "UiThreadDispatcher.h"

namespace oxygen::graphics {

  namespace System {
    ref class Action;
    ref class Object;
    ref class String;
    value struct Guid;
  }

  namespace System::Threading {
    ref class SynchronizationContext;
    ref class Thread;
    ref class SendOrPostCallback;
  }

  namespace System::Threading::Tasks {
    ref class Task;
    generic<typename TResult>
    ref class TaskCompletionSource;
  }
  class Surface;
}

namespace Oxygen::Editor::EngineInterface {

  // Forward declare the managed LogHandler so EngineRunner header does not expose
  // any logging implementation or native logging headers.
  ref class LogHandler;

  public
  ref class EngineRunner sealed {
  public:
    EngineRunner();
    ~EngineRunner(); // destructor
    !EngineRunner(); // finalizer (safety)

    /// <summary>
    /// Configures the native engine logging subsystem without binding a managed <c>ILogger</c>.
    /// Use this overload if you only need native logging (e.g., to <c>stderr</c> or files)
    /// and do not want managed log forwarding.
    /// </summary>
    /// <param name="config">
    /// The logging configuration, including verbosity, color settings, and per-module overrides.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if the native logging backend was initialized successfully;
    /// otherwise, <see langword="false"/>.
    /// </returns>
    /// <remarks>
    /// This method must be invoked before any native engine component emits log output
    /// you want captured. It is safe to call multiple times; subsequent calls will
    /// reconfigure verbosity and overrides. This overload does not create any managed
    /// reflection or delegate bindings.
    /// </remarks>
    auto ConfigureLogging(LoggingConfig^ config) -> bool;

    /// <summary>
    /// Configures the native engine logging subsystem and wires a managed
    /// <c>Microsoft.Extensions.Logging.ILogger</c> instance so native log messages
    /// are forwarded into the managed logging pipeline.
    /// </summary>
    /// <param name="config">
    /// The logging configuration, including verbosity, color settings, and per-module overrides.
    /// </param>
    /// <param name="logger">
    /// A managed <c>ILogger</c> instance (boxed as <c>System::Object^</c>) to receive
    /// forwarded native log events.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if the native logging backend was initialized successfully;
    /// otherwise, <see langword="false"/>.
    /// </returns>
    /// <remarks>
    /// <para>
    /// On success, a native callback is registered that captures each native log message,
    /// maps its verbosity to <c>LogLevel</c>, and invokes <c>ILogger.Log(...)</c>
    /// via cached reflection metadata. Reflection discovery of the <c>Log</c> method
    /// and construction of a formatter delegate occur only on the first successful call.
    /// </para>
    /// <para>
    /// If <paramref name="logger"/> is <see langword="nullptr"/>, this overload behaves
    /// the same as the simpler overload.
    /// </para>
    /// <para>
    /// Safe to call multiple times; the logger reference and cached method info are replaced.
    /// </para>
    /// </remarks>
    auto ConfigureLogging(LoggingConfig^ config, Object^ logger) -> bool;

    /// <summary>
    /// Creates and initializes the engine using the supplied configuration.
    /// </summary>
    /// <param name="config">
    /// The engine configuration to use during initialization.
    /// </param>
    /// <param name="swapChainPanel">
    /// The native IUnknown pointer to the WinUI 3 SwapChainPanel to render into. If IntPtr::Zero, the engine will run in headless mode or create its own window (depending on config).
    /// </param>
    /// <returns>
    /// A new EngineContext if creation succeeded; otherwise, nullptr.
    /// </returns>
    auto CreateEngine(EngineConfig^ config, System::IntPtr swapChainPanel) -> EngineContext^;

    /// <summary>
    /// Creates and initializes the engine using the supplied configuration (headless or default window).
    /// </summary>
    auto CreateEngine(EngineConfig^ config) -> EngineContext^;

    auto RunEngine(EngineContext^ ctx) -> void;

    /// <summary>
    /// Starts the engine loop on a dedicated background thread and returns a task that
    /// completes when the engine stops.
    /// </summary>
    auto RunEngineAsync(EngineContext^ ctx)
      -> System::Threading::Tasks::Task^;

    auto StopEngine(EngineContext^ ctx) -> void;

    auto RegisterSurface(EngineContext^ ctx, System::Guid documentId,
      System::Guid viewportId, System::String^ displayName,
      System::IntPtr swapChainPanel) -> bool;

    auto ResizeSurface(System::Guid viewportId, System::UInt32 width,
      System::UInt32 height) -> void;

    auto UnregisterSurface(System::Guid viewportId) -> void;

    void CaptureUiSynchronizationContext();

  private:
    // Encapsulated logging handler (forward-declared above). This hides any
    // references to native logging libraries from this header.
    LogHandler^ log_handler_;

    bool disposed_;
    std::shared_ptr<SurfaceRegistry>* surface_registry_;

    UiThreadDispatcher^ ui_dispatcher_;
    RenderThreadContext^ render_thread_context_;
    System::Threading::Tasks::Task^ engine_task_;
    System::Threading::Tasks::TaskCompletionSource<bool>^ engine_completion_source_;
    EngineContext^ active_context_;
    System::Object^ state_lock_;

    void EngineLoopAdapter(System::Object^ state);
    void OnEngineLoopExited();
    void DispatchToUi(System::Action^ action);
    void DispatchToUi(System::Threading::SendOrPostCallback^ callback,
      System::Object^ state);
    void SendToUi(System::Threading::SendOrPostCallback^ callback,
      System::Object^ state);
    void InvokeAction(System::Object^ action);
    void ResetSurfaceRegistry();
    void EnsureSurfaceRegistry();
    auto GetSurfaceRegistry() -> std::shared_ptr<SurfaceRegistry>;
    static auto ToGuidKey(System::Guid guid) -> SurfaceRegistry::GuidKey;
    void EnsureEngineLoopStopped();
    void AttachSwapChain(System::IntPtr panelPtr, System::IntPtr swapChainPtr);
    void AttachSwapChainCallback(System::Object^ state);
  };

} // namespace Oxygen::Editor::EngineInterface
