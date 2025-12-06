//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, on)

#include "Config.h"

using System::Object;
using System::String;
using System::IntPtr;

namespace Microsoft::Extensions::Logging {
  interface class ILogger;
}
namespace loguru {
  struct Message;
}

namespace Oxygen::Interop {

  // Managed helper that encapsulates all logging-related state and behavior so
  // the EngineRunner header doesn't need to include or reference native logging
  // internals.
  public
  ref class LogHandler sealed {
  public:
    LogHandler();
    ~LogHandler() { ReleaseCallback(); }
    !LogHandler() { ReleaseCallback(); }

    void SetLogger(Object^ logger);
    bool ConfigureLogging(LoggingConfig^ config);
    LoggingConfig^ GetCurrentConfig();

    // Invoked from native forwarder through the GCHandle.
    void HandleLog(const loguru::Message& message);

  private:
    void RegisterCallbackIfNeeded();
    void ReleaseCallback();
    static String^ Format(String^ state, System::Exception^ ex);

    // Managed logger to forward native messages
    Microsoft::Extensions::Logging::ILogger^ logger_;
    // Store the last applied LoggingConfig
    LoggingConfig^ current_config_;
    bool callback_registered_;
    // GCHandle to this (for callback user_data)
    IntPtr self_handle_;
  };

} // namespace Oxygen::Interop

#pragma managed(pop)
