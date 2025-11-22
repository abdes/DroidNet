//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed

namespace System {
  ref class Object;
  ref class String;
}

namespace System::Threading {
  ref class SynchronizationContext;
  ref class Thread;
  ref class SendOrPostCallback;
}

namespace Oxygen::Editor::EngineInterface {

  /// <summary>
  /// Captures and enforces access to the UI thread <see cref="SynchronizationContext"/>.
  /// </summary>
  public ref class UiThreadDispatcher sealed {
  public:
    UiThreadDispatcher();

    /// <summary>
    /// Gets a value indicating whether a synchronization context has been captured.
    /// </summary>
    property bool IsCaptured {
      bool get();
    }

    /// <summary>
    /// Captures <see cref="SynchronizationContext::Current"/> or throws if none is available.
    /// </summary>
    void CaptureCurrentOrThrow(System::String^ operationDescription);

    /// <summary>
    /// Captures the specified context or throws if it is <see langword="nullptr"/>.
    /// </summary>
    void Capture(System::Threading::SynchronizationContext^ context,
      System::String^ operationDescription);

    /// <summary>
    /// Verifies that the current thread owns the captured context.
    /// </summary>
    void VerifyAccess(System::String^ operationDescription);

    void Post(System::Threading::SendOrPostCallback^ callback, System::Object^ state);
    void Send(System::Threading::SendOrPostCallback^ callback, System::Object^ state);

  private:
    void EnsureCapturedOrThrow(System::String^ operationDescription);

    System::Threading::SynchronizationContext^ context_;
    int captured_thread_id_;
  };

} // namespace Oxygen::Editor::EngineInterface
