//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

struct OxygenEditorForwarder;
// Forward declare loguru message struct to avoid including logging headers in
// managed header.
namespace loguru {
struct Message;
}

namespace Oxygen::Editor::EngineInterface {

public
ref class LoggingConfig sealed {
public:
  LoggingConfig() {
    // default verbosity (OFF)
    Verbosity = -9;
    IsColored = false;
    ModuleOverrides = gcnew System::String("");
  }

  property int Verbosity;
  property bool IsColored;
  property System::String ^ ModuleOverrides;
};

public
ref class EngineConfig sealed{
  public : EngineConfig(){}

};

public
ref class EngineRunner sealed {
public:
  EngineRunner();
  ~EngineRunner(); // destructor
  !EngineRunner(); // finalizer (safety)

  /// <summary>
  /// Configure native logging (no managed forwarding). CONTRACT: Call before
  /// any engine logs.
  /// </summary>
  auto ConfigureLogging(LoggingConfig ^ config) -> bool;

  /// <summary>
  /// Configure native logging and bind a managed ILogger (passed as
  /// System::Object^). Reflection discovery and delegate caching are done here
  /// once.
  /// </summary>
  auto ConfigureLogging(LoggingConfig ^ config, Object ^ logger)
      -> bool; // logger: Microsoft.Extensions.Logging.ILogger

  auto CreateEngine(EngineConfig ^ config) -> bool;

  auto HandleLog(const loguru::Message &message)
      -> void; // instance log handler

private:
  auto CacheLoggerArtifacts() -> void;
  auto RegisterCallbackIfNeeded() -> void;
  auto ReleaseCallback() -> void;

  // Instance state
  Object ^ _logger;
  System::Reflection::MethodInfo ^ _logMethod;
  System::Delegate ^ _formatterDelegate;
  bool _callbackRegistered;
  System::IntPtr _selfHandle; // GCHandle to this (for callback user_data)
  bool _disposed;
};

} // namespace Oxygen::Editor::EngineInterface
