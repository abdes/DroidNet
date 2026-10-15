// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.World.Play;

#pragma warning disable SA1204 // Each log method keeps its static and instance forms together

/// <summary>Logger message definitions for <see cref="PlaySessionService"/>.</summary>
public sealed partial class PlaySessionService
{
    [LoggerMessage(Level = LogLevel.Information, Message = "Play requested for scene '{Scene}': saving and cooking.")]
    private partial void LogPlayRequested(string scene);

    [LoggerMessage(Level = LogLevel.Information, Message = "Wrote the player launch file '{Path}': {RootCount} cooked roots, scene '{Scene}'.")]
    private partial void LogLaunchFileWritten(string path, int rootCount, string scene);

    [LoggerMessage(Level = LogLevel.Information, Message = "Playing scene '{Scene}'.")]
    private partial void LogPlaying(string scene);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Play of scene '{Scene}' was cancelled before the session began.")]
    private partial void LogPlayCancelled(string scene);

    [LoggerMessage(Level = LogLevel.Information, Message = "Stop requested (scene '{Scene}').")]
    private partial void LogStopRequested(string scene);

    [LoggerMessage(Level = LogLevel.Error, Message = "Play failed ({Code}): {Message}")]
    private partial void LogPlayFailed(string code, string message);

    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "Player lifecycle: {State} '{Scene}'.")]
    private static partial void LogLifecycle(ILogger logger, string state, string scene);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogLifecycle(string state, string scene) => LogLifecycle(this.logger, state, scene);
}
