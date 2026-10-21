// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Logging helpers for <see cref="EngineShutdownService"/>.</summary>
public sealed partial class EngineShutdownService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Engine shutdown failed. Remaining runtime state: {State}.")]
    private static partial void LogShutdownFailed(ILogger logger, Exception exception, EngineServiceState state);

    private void LogShutdownFailed(Exception exception, EngineServiceState state)
        => LogShutdownFailed(this.logger, exception, state);
}
