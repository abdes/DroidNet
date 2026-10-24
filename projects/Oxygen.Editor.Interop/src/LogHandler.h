//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Config.h"

#pragma managed(push, off)
namespace loguru {
  struct Message;
}
#pragma managed(pop)

#pragma managed(push, on)

using System::Object;
using System::String;
using System::IntPtr;

namespace Microsoft::Extensions::Logging {
  interface class ILogger;
}

namespace Oxygen::Interop {

  // A structured log property, and the state of one forwarded engine message:
  // its properties, which the managed logger turns into event properties.
  typedef System::Collections::Generic::KeyValuePair<String^, Object^>
    LogProperty;
  typedef System::Collections::Generic::List<LogProperty> LogState;

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
    static auto MakeLogState(const loguru::Message& message, String^ text)
      -> LogState^;
    static String^ Format(LogState^ state, System::Exception^ ex);

    literal String^ kMessageProperty = "Message";

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
