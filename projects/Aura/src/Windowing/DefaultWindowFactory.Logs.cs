// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Aura.Windowing;

/// <summary>
///     Logging helpers for <see cref="DefaultWindowFactory"/>.
/// </summary>
public sealed partial class DefaultWindowFactory
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Creating window of type {WindowType} with metadata: {WidthMetadata}.")]
    private static partial void LogCreateWindow(ILogger logger, string windowType, bool widthMetadata);

    [Conditional("DEBUG")]
    private void LogCreateWindow(Type windowType, bool widthMetadata)
        => LogCreateWindow(this.logger, windowType.FullName ?? "<unknown>", widthMetadata);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Created window of type {WindowType}.")]
    private static partial void LogWindowCreated(ILogger logger, string windowType);

    private void LogWindowCreated(Type windowType)
        => LogWindowCreated(this.logger, windowType.FullName ?? "<unknown>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to create window of type {WindowType}.")]
    private static partial void LogCreateWindowFailed(ILogger logger, Exception exception, string windowType);

    private void LogCreateWindowFailed(Exception exception, Type windowType)
        => LogCreateWindowFailed(this.logger, exception, windowType.FullName ?? "<unknown>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Creating keyed window with key {Key} and metadata: {WidthMetadata}.")]
    private static partial void LogCreateKeyedWindow(ILogger logger, string key, bool widthMetadata);

    [Conditional("DEBUG")]
    private void LogCreateKeyedWindow(string key, bool widthMetadata)
        => LogCreateKeyedWindow(this.logger, key, widthMetadata);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Created keyed window with key {Key} of type {WindowType} and metadata: {WidthMetadata}.")]
    private static partial void LogKeyedWindowCreated(ILogger logger, string key, string windowType, bool widthMetadata);

    private void LogKeyedWindowCreated(string key, Type windowType, bool widthMetadata)
        => LogKeyedWindowCreated(this.logger, key, windowType.FullName ?? "<unknown>", widthMetadata);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to create keyed window with key {Key}.")]
    private static partial void LogCreateKeyedWindowFailed(ILogger logger, Exception exception, string key);

    private void LogCreateKeyedWindowFailed(Exception exception, string key)
        => LogCreateKeyedWindowFailed(this.logger, exception, key);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Creating decorated window for category {Category} with metadata: {WidthMetadata}.")]
    private static partial void LogCreateDecoratedWindow(ILogger logger, WindowCategory category, bool widthMetadata);

    [Conditional("DEBUG")]
    private void LogCreateDecoratedWindow(WindowCategory category, bool widthMetadata)
        => LogCreateDecoratedWindow(this.logger, category, widthMetadata);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Created decorated window for category {Category} of type {WindowType} with metadata: {WidthMetadata}.")]
    private static partial void LogDecoratedWindowCreated(ILogger logger, WindowCategory category, string windowType, bool widthMetadata);

    private void LogDecoratedWindowCreated(WindowCategory category, Type windowType, bool widthMetadata)
        => LogDecoratedWindowCreated(this.logger, category, windowType.FullName ?? "<unknown>", widthMetadata);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to create decorated window of type {WindowType} for category {Category}.")]
    private static partial void LogCreateDecoratedWindowFailed(ILogger logger, Exception exception, string windowType, string category);

    private void LogCreateDecoratedWindowFailed(Exception exception, Type windowType, string category)
        => LogCreateDecoratedWindowFailed(this.logger, exception, windowType.FullName ?? "<unknown>", category);
}
