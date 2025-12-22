//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma managed

#include "UiThreadDispatcher.h"

using namespace System;
using namespace System::Threading;

namespace Oxygen::Interop {

  UiThreadDispatcher::UiThreadDispatcher()
    : captured_context_(nullptr), captured_thread_id_(-1) {
  }

  bool UiThreadDispatcher::IsCaptured::get() {
    return captured_context_ != nullptr;
  }

  void UiThreadDispatcher::CaptureCurrent(String^ operation) {
    auto current = SynchronizationContext::Current;
    Capture(current, operation);
  }

  void UiThreadDispatcher::Capture(SynchronizationContext^ context,
    String^ operation) {
    if (context == nullptr) {
      throw gcnew InvalidOperationException(String::Format(
        "{0} requires a valid SynchronizationContext on the current thread. "
        "Call CaptureUiSynchronizationContext() from the UI thread before "
        "headless runs.",
        operation));
    }

    captured_context_ = context;
    auto thread = Thread::CurrentThread;
    captured_thread_id_ = thread != nullptr ? thread->ManagedThreadId : -1;
  }

  void UiThreadDispatcher::VerifyAccess(String^ operation) {
    if (!IsCaptured) {
      throw gcnew InvalidOperationException(String::Format(
        "{0} requires a captured UI SynchronizationContext. Call "
        "CreateEngine() "
        "from the UI thread or CaptureUiSynchronizationContext() first.",
        operation));
    }

    auto current = Thread::CurrentThread;
    auto current_id = current != nullptr ? current->ManagedThreadId : -1;
    if (current_id != captured_thread_id_) {
      throw gcnew InvalidOperationException(
        String::Format("{0} must be invoked from the thread that captured the "
          "SynchronizationContext.",
          operation));
    }
  }

  void UiThreadDispatcher::Post(SendOrPostCallback^ callback, Object^ state) {
    if (callback == nullptr) {
      return;
    }

    if (!IsCaptured) {
      callback->Invoke(state);
      return;
    }

    captured_context_->Post(callback, state);
  }

  void UiThreadDispatcher::Send(SendOrPostCallback^ callback, Object^ state) {
    if (callback == nullptr) {
      return;
    }

    if (!IsCaptured) {
      callback->Invoke(state);
      return;
    }

    captured_context_->Send(callback, state);
  }

} // namespace Oxygen::Interop
