//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, on)

#include <memory>

#include <Config.h>
#include <EditorModule/SurfaceRegistry.h>
#include <EngineContext.h>
#include <RenderThreadContext.h>
#include <UiThreadDispatcher.h>
#include <Views/ViewConfigManaged.h>
#include <Views/ViewIdManaged.h>

namespace oxygen::graphics {

  namespace System {
    ref class Action;
    ref class Object;
    ref class String;
    value struct Guid;
  } // namespace System

  namespace System::Threading {
    ref class SynchronizationContext;
    ref class Thread;
    ref class SendOrPostCallback;
  } // namespace System::Threading

  namespace System::Threading::Tasks {
    ref class Task;
    generic<typename TResult> ref class TaskCompletionSource;
  } // namespace System::Threading::Tasks
  class Surface;
} // namespace oxygen::graphics

namespace Oxygen::Interop {

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
    /// Configures the native engine logging subsystem without binding a managed
    /// <c>ILogger</c>. Use this overload if you only need native logging (e.g.,
    /// to <c>stderr</c> or files) and do not want managed log forwarding.
    /// </summary>
    /// <param name="config">
    /// The logging configuration, including verbosity, color settings, and
    /// per-module overrides.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if the native logging backend was initialized
    /// successfully; otherwise, <see langword="false"/>.
    /// </returns>
    /// <remarks>
    /// This method must be invoked before any native engine component emits log
    /// output you want captured. It is safe to call multiple times; subsequent
    /// calls will reconfigure verbosity and overrides. This overload does not
    /// create any managed reflection or delegate bindings.
    /// </remarks>
    auto ConfigureLogging(LoggingConfig^ config) -> bool;
    auto GetLoggingConfig(EngineContext^ ctx) -> LoggingConfig^;

    /// <summary>
    /// Configures the native engine logging subsystem and wires a managed
    /// <c>Microsoft.Extensions.Logging.ILogger</c> instance so native log
    /// messages are forwarded into the managed logging pipeline.
    /// </summary>
    /// <param name="config">
    /// The logging configuration, including verbosity, color settings, and
    /// per-module overrides.
    /// </param>
    /// <param name="logger">
    /// A managed <c>ILogger</c> instance (boxed as <c>System::Object^</c>) to
    /// receive forwarded native log events.
    /// </param>
    /// <returns>
    /// <see langword="true"/> if the native logging backend was initialized
    /// successfully; otherwise, <see langword="false"/>.
    /// </returns>
    /// <remarks>
    /// <para>
    /// On success, a native callback is registered that captures each native log
    /// message, maps its verbosity to <c>LogLevel</c>, and invokes
    /// <c>ILogger.Log(...)</c> via cached reflection metadata. Reflection
    /// discovery of the <c>Log</c> method and construction of a formatter
    /// delegate occur only on the first successful call.
    /// </para>
    /// <para>
    /// If <paramref name="logger"/> is <see langword="nullptr"/>, this overload
    /// behaves the same as the simpler overload.
    /// </para>
    /// <para>
    /// Safe to call multiple times; the logger reference and cached method info
    /// are replaced.
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
    /// The native IUnknown pointer to the WinUI 3 SwapChainPanel to render into.
    /// If IntPtr::Zero, the engine will run in headless mode or create its own
    /// window (depending on config).
    /// </param>
    /// <returns>
    /// A new EngineContext if creation succeeded; otherwise, nullptr.
    /// </returns>
    auto CreateEngine(EngineConfig^ config, System::IntPtr swapChainPanel)
      -> EngineContext
      ^
      ;

    /// <summary>
    /// Creates and initializes the engine using the supplied configuration
    /// (headless or default window).
    /// </summary>
    auto CreateEngine(EngineConfig^ config) -> EngineContext^;

    auto RunEngine(EngineContext^ ctx) -> void;

    /// <summary>
    /// Set runtime target FPS via interop. 0 = uncapped. Caller must ensure
    /// appropriate synchronization (UI thread) when invoking.
    /// </summary>
    auto SetTargetFps(EngineContext^ ctx, System::UInt32 fps) -> void;

    /// <summary>
    /// Reads the current native EngineConfig for inspection. Returns a managed
    /// `EngineConfig` object converted from the native config.
    /// </summary>
    auto GetEngineConfig(EngineContext^ ctx) -> EngineConfig^;

    /// <summary>
    /// Starts the engine loop on a dedicated background thread and returns a task
    /// that completes when the engine stops.
    /// </summary>
    auto RunEngineAsync(EngineContext^ ctx) -> System::Threading::Tasks::Task^;

    auto StopEngine(EngineContext^ ctx) -> void;

    // Async variants that return a processed acknowledgement once the engine
    // module has executed the requested work (processed during the next
    // engine frame). These are non-blocking on the UI thread and complete
    // after the engine has applied the Resize or scheduled the Destroy.

    auto TryRegisterSurfaceAsync(EngineContext^ ctx, System::Guid documentId,
      System::Guid viewportId,
      System::String^ displayName,
      System::IntPtr swapChainPanel,
      System::UInt32 initialWidth,
      System::UInt32 initialHeight,
      float compositionScale)
      -> System::Threading::Tasks::Task<bool>^
      ;
    auto TryUnregisterSurfaceAsync(System::Guid viewportId)
      -> System::Threading::Tasks::Task<bool>^
      ;
    auto TryResizeSurfaceAsync(System::Guid viewportId, System::UInt32 width,
      System::UInt32 height)
      -> System::Threading::Tasks::Task<bool>^
      ;

    /// <summary>
    /// Create a new Editor view asynchronously using the supplied managed
    /// <c>ViewConfigManaged</c>. Returns a Task that completes with the
    /// engine-assigned <c>ViewIdManaged</c> on success or an invalid
    /// <c>ViewIdManaged</c> on failure.
    /// </summary>
    auto TryCreateViewAsync(EngineContext^ ctx, ViewConfigManaged^ cfg)
      -> System::Threading::Tasks::Task<ViewIdManaged>^;

    /// <summary>
    /// Destroy a view previously created in the engine. This attempts to
    /// remove the view from the editor module and returns whether the
    /// request was accepted.
    /// </summary>
    auto TryDestroyViewAsync(EngineContext^ ctx, ViewIdManaged viewId)
      -> System::Threading::Tasks::Task<bool>^;

      /// <summary>
    /// Queue a Show operation for a managed `ViewIdManaged`.
    /// This is a fire-and-forget operation: the native module will execute the
    /// associated show command on the engine thread during the next frame so the
    /// change is applied in-frame and does not destabilize rendering.
    /// Returns a Task<bool> that completes immediately indicating whether the
    /// request was accepted (not whether the view has finished showing).
    /// </summary>
    auto TryShowViewAsync(EngineContext^ ctx, ViewIdManaged viewId)
      -> System::Threading::Tasks::Task<bool>^;

    /// <summary>
    /// Queue a Hide operation for a managed `ViewIdManaged`.
    /// This operation is enqueued and executed on the engine thread during the
    /// next frame (fire-and-forget). The returned Task<bool> completes quickly
    /// to indicate the request was accepted; it does not imply the hide has
    /// already taken effect.
    /// </summary>
    auto TryHideViewAsync(EngineContext^ ctx, ViewIdManaged viewId)
      -> System::Threading::Tasks::Task<bool>^;

  private:
    // Encapsulated logging handler (forward-declared above). This hides any
    // references to native logging libraries from this header.
    LogHandler^ log_handler_;

    using SurfaceRegistry = oxygen::interop::module::SurfaceRegistry;

    bool disposed_;
    std::shared_ptr<SurfaceRegistry>* surface_registry_;

    UiThreadDispatcher^ ui_dispatcher_;
    RenderThreadContext^ render_thread_context_;
    System::Threading::Tasks::Task^ engine_task_;
    System::Threading::Tasks::TaskCompletionSource<bool>^
      engine_completion_source_;
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
    void AttachSwapChain(System::IntPtr panelPtr, System::IntPtr swapChainPtr,
      System::IntPtr surfaceHandle, float compositionScale);
    void AttachSwapChainCallback(System::Object^ state);
  };

} // namespace Oxygen::Interop

#pragma managed(pop)
