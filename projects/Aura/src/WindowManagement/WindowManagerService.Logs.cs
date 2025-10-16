// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Aura.WindowManagement;

#pragma warning disable SA1204 // Static elements should appear before instance elements

/// <summary>
///     Logging helpers for <see cref="WindowManagerService"/>.
/// </summary>
public sealed partial class WindowManagerService
{
    [LoggerMessage(
        EventId = 4100,
        Level = LogLevel.Information,
        Message = "[WindowManager] service initialized")]
    private static partial void LogServiceInitialized(ILogger logger);

    private void LogServiceInitialized()
        => LogServiceInitialized(this.logger);

    [LoggerMessage(
        EventId = 4101,
        Level = LogLevel.Information,
        Message = "[WindowManager] service disposing")]
    private static partial void LogServiceDisposing(ILogger logger);

    private void LogServiceDisposing()
        => LogServiceDisposing(this.logger);

    [LoggerMessage(
        EventId = 4110,
        Level = LogLevel.Debug,
        Message = "[WindowManager] creating window type={WindowType}")]
    private static partial void LogCreatingWindow(ILogger logger, string windowType);

    [Conditional("DEBUG")]
    private void LogCreatingWindow(string windowType)
        => LogCreatingWindow(this.logger, windowType);

    [LoggerMessage(
        EventId = 4111,
        Level = LogLevel.Information,
        Message = "[WindowManager] window created id={WindowId} type={WindowType} title={Title}")]
    private static partial void LogWindowCreated(ILogger logger, Guid windowId, string windowType, string title);

    private void LogWindowCreated(Guid windowId, string windowType, string? title)
        => LogWindowCreated(this.logger, windowId, windowType, title ?? string.Empty);

    [LoggerMessage(
        EventId = 4112,
        Level = LogLevel.Error,
        Message = "[WindowManager] failed to create window type={WindowType}")]
    private static partial void LogWindowCreateFailed(ILogger logger, Exception exception, string windowType);

    private void LogWindowCreateFailed(Exception exception, string windowType)
        => LogWindowCreateFailed(this.logger, exception, windowType);

    [LoggerMessage(
        EventId = 4120,
        Level = LogLevel.Warning,
        Message = "[WindowManager] attempted to close non-existent window id={WindowId}")]
    private static partial void LogCloseMissingWindow(ILogger logger, Guid windowId);

    private void LogCloseMissingWindow(Guid windowId)
        => LogCloseMissingWindow(this.logger, windowId);

    [LoggerMessage(
        EventId = 4121,
        Level = LogLevel.Debug,
        Message = "[WindowManager] closing window id={WindowId}")]
    private static partial void LogClosingWindow(ILogger logger, Guid windowId);

    [Conditional("DEBUG")]
    private void LogClosingWindow(Guid windowId)
        => LogClosingWindow(this.logger, windowId);

    [LoggerMessage(
        EventId = 4122,
        Level = LogLevel.Error,
        Message = "[WindowManager] error closing window id={WindowId}")]
    private static partial void LogCloseWindowFailed(ILogger logger, Exception exception, Guid windowId);

    private void LogCloseWindowFailed(Exception exception, Guid windowId)
        => LogCloseWindowFailed(this.logger, exception, windowId);

    [LoggerMessage(
        EventId = 4130,
        Level = LogLevel.Warning,
        Message = "[WindowManager] attempted to activate non-existent window id={WindowId}")]
    private static partial void LogActivateMissingWindow(ILogger logger, Guid windowId);

    private void LogActivateMissingWindow(Guid windowId)
        => LogActivateMissingWindow(this.logger, windowId);

    [LoggerMessage(
        EventId = 4131,
        Level = LogLevel.Error,
        Message = "[WindowManager] error activating window id={WindowId}")]
    private static partial void LogActivateWindowFailed(ILogger logger, Exception exception, Guid windowId);

    private void LogActivateWindowFailed(Exception exception, Guid windowId)
        => LogActivateWindowFailed(this.logger, exception, windowId);

    [LoggerMessage(
        EventId = 4140,
        Level = LogLevel.Information,
        Message = "[WindowManager] closing {Count} windows")]
    private static partial void LogClosingAllWindows(ILogger logger, int count);

    private void LogClosingAllWindows(int count)
        => LogClosingAllWindows(this.logger, count);

    [LoggerMessage(
        EventId = 4150,
        Level = LogLevel.Information,
        Message = "[WindowManager] window closed id={WindowId} title={Title}")]
    private static partial void LogWindowClosed(ILogger logger, Guid windowId, string title);

    private void LogWindowClosed(Guid windowId, string title)
        => LogWindowClosed(this.logger, windowId, title);

    [LoggerMessage(
        EventId = 4160,
        Level = LogLevel.Warning,
        Message = "[WindowManager] failed to enqueue theme application for window id={WindowId}")]
    private static partial void LogThemeApplyEnqueueFailed(ILogger logger, Guid windowId);

    private void LogThemeApplyEnqueueFailed(Guid windowId)
        => LogThemeApplyEnqueueFailed(this.logger, windowId);

    [LoggerMessage(
        EventId = 4161,
        Level = LogLevel.Error,
        Message = "[WindowManager] failed to apply theme to window id={WindowId}")]
    private static partial void LogThemeApplyFailed(ILogger logger, Exception exception, Guid windowId);

    private void LogThemeApplyFailed(Exception exception, Guid windowId)
        => LogThemeApplyFailed(this.logger, exception, windowId);

    [LoggerMessage(
        EventId = 4170,
        Level = LogLevel.Debug,
        Message = "[WindowManager] registering existing window type={WindowType}")]
    private static partial void LogRegisteringWindow(ILogger logger, string windowType);

    [Conditional("DEBUG")]
    private void LogRegisteringWindow(string windowType)
        => LogRegisteringWindow(this.logger, windowType);

    [LoggerMessage(
        EventId = 4171,
        Level = LogLevel.Information,
        Message = "[WindowManager] window registered id={WindowId} type={WindowType} title={Title}")]
    private static partial void LogWindowRegistered(ILogger logger, Guid windowId, string windowType, string title);

    private void LogWindowRegistered(Guid windowId, string windowType, string? title)
        => LogWindowRegistered(this.logger, windowId, windowType, title ?? string.Empty);

    [LoggerMessage(
        EventId = 4172,
        Level = LogLevel.Error,
        Message = "[WindowManager] failed to register window type={WindowType}")]
    private static partial void LogRegisterWindowFailed(ILogger logger, Exception exception, string windowType);

    private void LogRegisterWindowFailed(Exception exception, string windowType)
        => LogRegisterWindowFailed(this.logger, exception, windowType);

    [LoggerMessage(
        EventId = 4180,
        Level = LogLevel.Information,
        Message = "[WindowManager] router integration enabled")]
    private static partial void LogRouterIntegrationEnabled(ILogger logger);

    private void LogRouterIntegrationEnabled()
        => LogRouterIntegrationEnabled(this.logger);

    [LoggerMessage(
        EventId = 4185,
        Level = LogLevel.Debug,
        Message = "[WindowManager] router not available - router integration disabled")]
    private static partial void LogRouterNotAvailable(ILogger logger);

    [Conditional("DEBUG")]
    private void LogRouterNotAvailable()
        => LogRouterNotAvailable(this.logger);

    [LoggerMessage(
        EventId = 4181,
        Level = LogLevel.Debug,
        Message = "[WindowManager] tracking router window target={TargetName}")]
    private static partial void LogTrackingRouterWindow(ILogger logger, string targetName);

    [Conditional("DEBUG")]
    private void LogTrackingRouterWindow(string targetName)
        => LogTrackingRouterWindow(this.logger, targetName);

    [LoggerMessage(
        EventId = 4182,
        Level = LogLevel.Warning,
        Message = "[WindowManager] router window already tracked target={TargetName}")]
    private static partial void LogRouterWindowAlreadyTracked(ILogger logger, string targetName);

    private void LogRouterWindowAlreadyTracked(string targetName)
        => LogRouterWindowAlreadyTracked(this.logger, targetName);

    [LoggerMessage(
        EventId = 4183,
        Level = LogLevel.Error,
        Message = "[WindowManager] failed to track router window target={TargetName}")]
    private static partial void LogRouterWindowTrackingFailed(ILogger logger, Exception exception, string targetName);

    private void LogRouterWindowTrackingFailed(Exception exception, string targetName)
        => LogRouterWindowTrackingFailed(this.logger, exception, targetName);

    [LoggerMessage(
        EventId = 4184,
        Level = LogLevel.Debug,
        Message = "[WindowManager] router window destroyed target={TargetName}")]
    private static partial void LogRouterWindowDestroyed(ILogger logger, string targetName);

    [Conditional("DEBUG")]
    private void LogRouterWindowDestroyed(string targetName)
        => LogRouterWindowDestroyed(this.logger, targetName);
}
#pragma warning restore SA1204 // Static elements should appear before instance elements
