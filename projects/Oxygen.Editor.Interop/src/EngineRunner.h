//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed

#include "Config.h"
#include "EngineContext.h"

namespace Oxygen::Editor::EngineInterface {

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
      /// Configures the native engine logging subsystem without binding a managed <c>ILogger</c>.
      /// Use this overload if you only need native logging (e.g., to <c>stderr</c> or files)
      /// and do not want managed log forwarding.
      /// </summary>
      /// <param name="config">
      /// The logging configuration, including verbosity, color settings, and per-module overrides.
      /// </param>
      /// <returns>
      /// <see langword="true"/> if the native logging backend was initialized successfully;
      /// otherwise, <see langword="false"/>.
      /// </returns>
      /// <remarks>
      /// This method must be invoked before any native engine component emits log output
      /// you want captured. It is safe to call multiple times; subsequent calls will
      /// reconfigure verbosity and overrides. This overload does not create any managed
      /// reflection or delegate bindings.
      /// </remarks>
      auto ConfigureLogging(LoggingConfig^ config) -> bool;

      /// <summary>
      /// Configures the native engine logging subsystem and wires a managed
      /// <c>Microsoft.Extensions.Logging.ILogger</c> instance so native log messages
      /// are forwarded into the managed logging pipeline.
      /// </summary>
      /// <param name="config">
      /// The logging configuration, including verbosity, color settings, and per-module overrides.
      /// </param>
      /// <param name="logger">
      /// A managed <c>ILogger</c> instance (boxed as <c>System::Object^</c>) to receive
      /// forwarded native log events.
      /// </param>
      /// <returns>
      /// <see langword="true"/> if the native logging backend was initialized successfully;
      /// otherwise, <see langword="false"/>.
      /// </returns>
      /// <remarks>
      /// <para>
      /// On success, a native callback is registered that captures each native log message,
      /// maps its verbosity to <c>LogLevel</c>, and invokes <c>ILogger.Log(...)</c>
      /// via cached reflection metadata. Reflection discovery of the <c>Log</c> method
      /// and construction of a formatter delegate occur only on the first successful call.
      /// </para>
      /// <para>
      /// If <paramref name="logger"/> is <see langword="nullptr"/>, this overload behaves
      /// the same as the simpler overload.
      /// </para>
      /// <para>
      /// Safe to call multiple times; the logger reference and cached method info are replaced.
      /// </para>
      /// </remarks>
      auto ConfigureLogging(LoggingConfig^ config, Object^ logger) -> bool;

      /// <summary>
      /// Creates and initializes the engine using the supplied configuration.
      /// (Not yet implemented.)
      /// </summary>
      /// <param name="config">
      /// The engine configuration to use during initialization.
      /// </param>
      /// <returns>
      /// <see langword="true"/> if creation succeeded; otherwise, <see langword="false"/>.
      /// </returns>
      auto CreateEngine(EngineConfig^ config) -> EngineContext^;

      auto RunEngine(EngineContext^ ctx) -> void;

      auto StopEngine(EngineContext^ ctx) -> void;

    private:
      // Encapsulated logging handler (forward-declared above). This hides any
      // references to native logging libraries from this header.
      LogHandler^ log_handler_;

      bool disposed_;
  };

} // namespace Oxygen::Editor::EngineInterface
