// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor;

/// <summary>Logging helpers for <see cref="App" />.</summary>
public partial class App
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The application is stopping.")]
    private static partial void LogApplicationStopping(ILogger logger);

    private void LogApplicationStopping() => LogApplicationStopping(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The application stopped.")]
    private static partial void LogApplicationStopped(ILogger logger);

    private void LogApplicationStopped() => LogApplicationStopped(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Critical,
        Message = "The Project Explorer did not open at startup: the application stops.")]
    private static partial void LogStartupNavigationFailed(ILogger logger);

    private void LogStartupNavigationFailed() => LogStartupNavigationFailed(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Critical,
        Message = "An unhandled exception ends the editor.")]
    private static partial void LogUnhandledException(ILogger logger, Exception exception);

    private void LogUnhandledException(Exception exception) => LogUnhandledException(this.logger, exception);
}
