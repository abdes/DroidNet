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
} // namespace System

namespace System::Threading {
  ref class ParameterizedThreadStart;
  ref class Thread;
} // namespace System::Threading

namespace Oxygen::Interop {

  /// <summary>
  /// Manages the lifecycle of the dedicated engine render thread.
  /// </summary>
  public
  ref class RenderThreadContext sealed {
  public:
    RenderThreadContext();

    property bool IsRunning { bool get(); }

    property System::Threading::Thread^ ThreadHandle {
      System::Threading::Thread^ get();
    }

    void Start(System::Threading::ParameterizedThreadStart^ entryPoint,
      System::Object^ state, System::String^ threadName);

    void Clear();

    bool IsRenderThread();

    void Join();

  private:
    System::Threading::Thread^ thread_;
    System::Object^ gate_;
  };

} // namespace Oxygen::Interop

#pragma managed(pop)
