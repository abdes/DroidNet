//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma managed

#include "RenderThreadContext.h"

using namespace System;
using namespace System::Threading;

namespace Oxygen::Editor::EngineInterface {

  RenderThreadContext::RenderThreadContext()
    : thread_(nullptr)
    , gate_(gcnew Object())
  {
  }

  bool RenderThreadContext::IsRunning::get()
  {
    Monitor::Enter(gate_);
    try {
      return thread_ != nullptr && thread_->IsAlive;
    }
    finally {
      Monitor::Exit(gate_);
    }
  }

  Thread^ RenderThreadContext::ThreadHandle::get()
  {
    Monitor::Enter(gate_);
    try {
      return thread_;
    }
    finally {
      Monitor::Exit(gate_);
    }
  }

  void RenderThreadContext::Start(ParameterizedThreadStart^ entryPoint,
    Object^ state, String^ threadName)
  {
    if (entryPoint == nullptr) {
      throw gcnew ArgumentNullException("entryPoint");
    }

    Monitor::Enter(gate_);
    try {
      if (thread_ != nullptr && thread_->IsAlive) {
        throw gcnew InvalidOperationException("The engine render thread is already running.");
      }

      thread_ = gcnew Thread(entryPoint);
      thread_->IsBackground = true;
      if (!String::IsNullOrEmpty(threadName)) {
        thread_->Name = threadName;
      }
      thread_->Start(state);
    }
    finally {
      Monitor::Exit(gate_);
    }
  }

  void RenderThreadContext::Clear()
  {
    Monitor::Enter(gate_);
    try {
      thread_ = nullptr;
    }
    finally {
      Monitor::Exit(gate_);
    }
  }

  bool RenderThreadContext::IsRenderThread()
  {
    Monitor::Enter(gate_);
    try {
      return thread_ != nullptr && Thread::CurrentThread == thread_;
    }
    finally {
      Monitor::Exit(gate_);
    }
  }

  void RenderThreadContext::Join()
  {
    Thread^ thread = nullptr;

    Monitor::Enter(gate_);
    try {
      thread = thread_;
    }
    finally {
      Monitor::Exit(gate_);
    }

    if (thread != nullptr) {
      try {
        thread->Join();
      }
      catch (...) {
        // Swallow to avoid tearing down during shutdown.
      }
    }
  }

} // namespace Oxygen::Editor::EngineInterface
