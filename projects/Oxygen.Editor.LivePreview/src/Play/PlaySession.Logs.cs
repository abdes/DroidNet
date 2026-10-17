// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>
///     Logging helpers for <see cref="PlaySession"/>.
/// </summary>
public sealed partial class PlaySession
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Play requested for scene '{Scene}': saving and cooking.")]
    private static partial void LogPlayRequested(ILogger logger, string scene);

    private void LogPlayRequested(string scene)
        => LogPlayRequested(this.logger, scene);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Wrote the player launch file '{Path}': {RootCount} cooked roots, scene '{Scene}'.")]
    private static partial void LogLaunchFileWritten(ILogger logger, string path, int rootCount, string scene);

    private void LogLaunchFileWritten(string path, int rootCount, string scene)
        => LogLaunchFileWritten(this.logger, path, rootCount, scene);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Playing scene '{Scene}'.")]
    private static partial void LogPlaying(ILogger logger, string scene);

    private void LogPlaying(string scene)
        => LogPlaying(this.logger, scene);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Play of scene '{Scene}' was cancelled before the session began.")]
    private static partial void LogPlayCancelled(ILogger logger, string scene);

    private void LogPlayCancelled(string scene)
        => LogPlayCancelled(this.logger, scene);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Play refused: document {Document} is not an open scene document.")]
    private static partial void LogPlayRefusedNotAScene(ILogger logger, DroidNet.Documents.DocumentId document);

    private void LogPlayRefusedNotAScene(DroidNet.Documents.DocumentId document)
        => LogPlayRefusedNotAScene(this.logger, document);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Ending the player failed; its process may still run.")]
    private static partial void LogEndFailed(ILogger logger, Exception exception);

    private void LogEndFailed(Exception exception)
        => LogEndFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Stop requested (scene '{Scene}').")]
    private static partial void LogStopRequested(ILogger logger, string scene);

    private void LogStopRequested(string scene)
        => LogStopRequested(this.logger, scene);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Play failed ({Code}): {Message}")]
    private static partial void LogPlayFailed(ILogger logger, string code, string message);

    private void LogPlayFailed(string code, string message)
        => LogPlayFailed(this.logger, code, message);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Player lifecycle: {State} '{Scene}'.")]
    private static partial void LogLifecycle(ILogger logger, string state, string scene);

    [Conditional("DEBUG")]
    private void LogLifecycle(string state, string scene)
        => LogLifecycle(this.logger, state, scene);
}
