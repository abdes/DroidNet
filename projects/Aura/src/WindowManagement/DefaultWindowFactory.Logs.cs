// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Aura.WindowManagement;

#pragma warning disable SA1204 // Static elements should appear before instance elements

/// <summary>
///     Logging helpers for <see cref="DefaultWindowFactory"/>.
/// </summary>
public sealed partial class DefaultWindowFactory
{
    [LoggerMessage(
        EventId = 4200,
        Level = LogLevel.Debug,
        Message = "[DefaultWindowFactory] resolving window type {WindowType}")]
    private static partial void LogResolvingWindow(ILogger logger, string windowType);

    [Conditional("DEBUG")]
    private void LogResolvingWindow(string windowType)
        => LogResolvingWindow(this.logger, windowType);

    [LoggerMessage(
        EventId = 4201,
        Level = LogLevel.Debug,
        Message = "[DefaultWindowFactory] resolved window type {WindowType}")]
    private static partial void LogResolvedWindow(ILogger logger, string windowType);

    [Conditional("DEBUG")]
    private void LogResolvedWindow(string windowType)
        => LogResolvedWindow(this.logger, windowType);

    [LoggerMessage(
        EventId = 4210,
        Level = LogLevel.Error,
        Message = "[DefaultWindowFactory] failed to create window of type {WindowType}")]
    private static partial void LogCreateWindowFailed(ILogger logger, Exception exception, string windowType);

    private void LogCreateWindowFailed(Exception exception, string windowType)
        => LogCreateWindowFailed(this.logger, exception, windowType);

    [LoggerMessage(
        EventId = 4220,
        Level = LogLevel.Debug,
        Message = "[DefaultWindowFactory] resolving window by name {TypeName}")]
    private static partial void LogResolvingWindowByName(ILogger logger, string typeName);

    [Conditional("DEBUG")]
    private void LogResolvingWindowByName(string typeName)
        => LogResolvingWindowByName(this.logger, typeName);

    [LoggerMessage(
        EventId = 4221,
        Level = LogLevel.Debug,
        Message = "[DefaultWindowFactory] resolved window by name {TypeName}")]
    private static partial void LogResolvedWindowByName(ILogger logger, string typeName);

    private void LogResolvedWindowByName(string typeName)
        => LogResolvedWindowByName(this.logger, typeName);

    [LoggerMessage(
        EventId = 4230,
        Level = LogLevel.Error,
        Message = "[DefaultWindowFactory] failed to create window by name {TypeName}")]
    private static partial void LogCreateWindowByNameFailed(ILogger logger, Exception exception, string typeName);

    private void LogCreateWindowByNameFailed(Exception exception, string typeName)
        => LogCreateWindowByNameFailed(this.logger, exception, typeName);

    [LoggerMessage(
        EventId = 4240,
        Level = LogLevel.Warning,
        Message = "[DefaultWindowFactory] TryCreate failed for window type {WindowType}")]
    private static partial void LogTryCreateWindowFailed(ILogger logger, Exception exception, string windowType);

    private void LogTryCreateWindowFailed(Exception exception, string windowType)
        => LogTryCreateWindowFailed(this.logger, exception, windowType);
}
#pragma warning restore SA1204 // Static elements should appear before instance elements
