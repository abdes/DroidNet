// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace DroidNet.Aura.Windowing;

/// <summary>
///     Logging helpers for <see cref="DefaultWindowFactory"/>.
/// </summary>
public sealed partial class DefaultWindowFactory
{
    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Creating window of type: {WindowType}")]
    private static partial void LogCreateWindow(ILogger logger, string windowType);

    private void LogCreateWindow(Type windowType)
        => LogCreateWindow(this.logger, windowType.FullName ?? "<unknown>");

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Window created successfully: {WindowType}")]
    private static partial void LogWindowCreated(ILogger logger, string windowType);

    private void LogWindowCreated(Type windowType)
        => LogWindowCreated(this.logger, windowType.FullName ?? "<unknown>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to create window of type: {WindowType}")]
    private static partial void LogCreateWindowFailed(ILogger logger, Exception exception, string windowType);

    private void LogCreateWindowFailed(Exception exception, Type windowType)
        => LogCreateWindowFailed(this.logger, exception, windowType.FullName ?? "<unknown>");

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Creating keyed window with key: {Key}")]
    private static partial void LogCreateKeyedWindow(ILogger logger, string key);

    private void LogCreateKeyedWindow(string key)
        => LogCreateKeyedWindow(this.logger, key);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Keyed window created successfully. Key: {Key}, Type: {WindowType}")]
    private static partial void LogKeyedWindowCreated(ILogger logger, string key, string windowType);

    private void LogKeyedWindowCreated(string key, Type windowType)
        => LogKeyedWindowCreated(this.logger, key, windowType.FullName ?? "<unknown>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to create keyed window with key: {Key}")]
    private static partial void LogCreateKeyedWindowFailed(ILogger logger, Exception exception, string key);

    private void LogCreateKeyedWindowFailed(Exception exception, string key)
        => LogCreateKeyedWindowFailed(this.logger, exception, key);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Creating decorated window. Category: {Category}")]
    private static partial void LogCreateDecoratedWindow(ILogger logger, string category);

    private void LogCreateDecoratedWindow(WindowCategory category)
        => LogCreateDecoratedWindow(this.logger, category.Value);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Decorated window created successfully. Category: {Category}, Type: {WindowType}")]
    private static partial void LogDecoratedWindowCreated(ILogger logger, string category, string windowType);

    private void LogDecoratedWindowCreated(WindowCategory category, Type windowType)
        => LogDecoratedWindowCreated(this.logger, category.Value, windowType.FullName ?? "<unknown>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to create decorated window. Category: {Category}, Type: {WindowType}")]
    private static partial void LogCreateDecoratedWindowFailed(ILogger logger, Exception exception, string windowType, string category);

    private void LogCreateDecoratedWindowFailed(Exception exception, Type windowType, string category)
        => LogCreateDecoratedWindowFailed(this.logger, exception, windowType.FullName ?? "<unknown>", category);
}
