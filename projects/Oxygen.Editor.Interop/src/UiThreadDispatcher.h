//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

namespace System {
  ref class Object;
  ref class String;
  ref class ArgumentNullException;
  ref class InvalidOperationException;
} // namespace System

namespace System::Threading {
  ref class SynchronizationContext;
  ref class SendOrPostCallback;
} // namespace System::Threading

namespace Oxygen::Interop {

  /// <summary>
  ///   Captures and enforces access to the UI thread <see
  ///   cref="SynchronizationContext"/>.
  /// </summary>
  /// <remarks>
  ///   Use an instance of this class to capture the UI thread synchronization
  ///   context once (for example during initialization) and then verify or
  ///   marshal calls to that context from other threads using <see cref="Post"/>
  ///   and <see cref="Send"/>.
  /// </remarks>
  public
  ref class UiThreadDispatcher sealed {
  public:
    /// <summary>
    ///   Initializes a new instance of the <see cref="UiThreadDispatcher"/>
    ///   class. No context is captured by the constructor; call
    ///   <see cref="CaptureCurrent"/> or <see cref="Capture"/> to capture a
    ///   context.
    /// </summary>
    UiThreadDispatcher();

    /// <summary>
    ///   Gets a value indicating whether a synchronization context has been
    ///   captured by this dispatcher.
    /// </summary>
    property bool IsCaptured { bool get(); }

    /// <summary>
    ///   Captures <see cref="SynchronizationContext::Current"/> for later use.
    /// </summary>
    /// <param name="operation">
    ///   Short human-readable description of the operation requesting the
    ///   capture. Used to improve diagnostic messages if capturing fails.
    /// </param>
    /// <exception cref="System::InvalidOperationException">
    ///   Thrown when <see cref="SynchronizationContext::Current"/> is
    ///   <see langword="null"/> (nothing to capture).
    /// </exception>
    void CaptureCurrent(System::String^ operation);

    /// <summary>
    ///   Captures the specified synchronization <paramref name="context"/> for
    ///   later verification and dispatch.
    /// </summary>
    /// <param name="context">The synchronization context to capture.</param>
    /// <param name="operation">
    ///   Short human-readable description of the operation requesting the
    ///   capture. Used to improve diagnostic messages if capturing fails.
    /// </param>
    /// <exception cref="System::ArgumentNullException">
    ///   Thrown when <paramref name="context"/> is <see langword="null"/>.
    /// </exception>
    void Capture(System::Threading::SynchronizationContext^ context,
      System::String^ operation);

    /// <summary>
    ///   Verifies that the current thread is the owner of the captured
    ///   synchronization context.
    /// </summary>
    /// <param name="operation">
    ///   Short human-readable description of the operation requiring access.
    ///   Used to improve diagnostic messages if verification fails.
    /// </param>
    /// <exception cref="System::InvalidOperationException">
    ///   Thrown when no synchronization context has been captured, or when the
    ///   current thread does not own the captured context.
    /// </exception>
    void VerifyAccess(System::String^ operation);

    /// <summary>
    ///   Posts a callback to the captured synchronization context to be invoked
    ///   asynchronously.
    /// </summary>
    /// <param name="callback">The callback to invoke on the UI thread.</param>
    /// <param name="state">Optional state passed to the callback.</param>
    /// <exception cref="System::ArgumentNullException">
    ///   Thrown when <paramref name="callback"/> is <see langword="null"/>.
    /// </exception>
    /// <exception cref="System::InvalidOperationException">
    ///   Thrown when no synchronization context has been captured.
    /// </exception>
    void Post(System::Threading::SendOrPostCallback^ callback,
      System::Object^ state);

    /// <summary>
    ///   Sends a callback to the captured synchronization context to be invoked
    ///   synchronously (waits for completion).
    /// </summary>
    /// <param name="callback">The callback to invoke on the UI thread.</param>
    /// <param name="state">Optional state passed to the callback.</param>
    /// <exception cref="System::ArgumentNullException">
    ///   Thrown when <paramref name="callback"/> is <see langword="null"/>.
    /// </exception>
    /// <exception cref="System::InvalidOperationException">
    ///   Thrown when no synchronization context has been captured.
    /// </exception>
    void Send(System::Threading::SendOrPostCallback^ callback,
      System::Object^ state);

  private:
    System::Threading::SynchronizationContext^ captured_context_;
    int captured_thread_id_;
  };

} // namespace Oxygen::Interop

#pragma managed(pop)
