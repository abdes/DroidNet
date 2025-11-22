//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma managed

#include "UiThreadDispatcher.h"

using namespace System;
using namespace System::Threading;

namespace Oxygen::Editor::EngineInterface {

  UiThreadDispatcher::UiThreadDispatcher()
    : context_(nullptr)
    , captured_thread_id_(-1)
  {
  }

  bool UiThreadDispatcher::IsCaptured::get()
  {
    return context_ != nullptr;
  }

  void UiThreadDispatcher::CaptureCurrentOrThrow(String^ operationDescription)
  {
    auto current = SynchronizationContext::Current;
    Capture(current, operationDescription);
  }

  void UiThreadDispatcher::Capture(SynchronizationContext^ context,
    String^ operationDescription)
  {
    if (context == nullptr) {
      throw gcnew InvalidOperationException(String::Format(
        "{0} requires a valid SynchronizationContext on the current thread. "
        "Call CaptureUiSynchronizationContext() from the UI thread before headless runs.",
        operationDescription));
    }

    context_ = context;
    auto thread = Thread::CurrentThread;
    captured_thread_id_ = thread != nullptr ? thread->ManagedThreadId : -1;
  }

  void UiThreadDispatcher::VerifyAccess(String^ operationDescription)
  {
    EnsureCapturedOrThrow(operationDescription);

    auto current = Thread::CurrentThread;
    auto current_id = current != nullptr ? current->ManagedThreadId : -1;
    if (current_id != captured_thread_id_) {
      throw gcnew InvalidOperationException(String::Format(
        "{0} must be invoked from the thread that captured the SynchronizationContext.",
        operationDescription));
    }
  }

  void UiThreadDispatcher::Post(SendOrPostCallback^ callback, Object^ state)
  {
    if (callback == nullptr) {
      return;
    }

    if (!IsCaptured) {
      callback->Invoke(state);
      return;
    }

    context_->Post(callback, state);
  }

  void UiThreadDispatcher::Send(SendOrPostCallback^ callback, Object^ state)
  {
    if (callback == nullptr) {
      return;
    }

    if (!IsCaptured) {
      callback->Invoke(state);
      return;
    }

    context_->Send(callback, state);
  }

  void UiThreadDispatcher::EnsureCapturedOrThrow(String^ operationDescription)
  {
    if (!IsCaptured) {
      throw gcnew InvalidOperationException(String::Format(
        "{0} requires a captured UI SynchronizationContext. Call CreateEngine() "
        "from the UI thread or CaptureUiSynchronizationContext() first.",
        operationDescription));
    }
  }

} // namespace Oxygen::Editor::EngineInterface
