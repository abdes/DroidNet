// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.UI;

namespace DroidNet.Aura.Windowing;

/// <summary>
///     Logging helpers for <see cref="WindowManagerService"/>.
/// </summary>
public sealed partial class WindowManagerService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The window manager service has been initialized.")]
    private static partial void LogServiceInitialized(ILogger logger);

    private void LogServiceInitialized()
        => LogServiceInitialized(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The window manager service is being disposed.")]
    private static partial void LogServiceDisposing(ILogger logger);

    private void LogServiceDisposing() => LogServiceDisposing(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "An attempt was made to close a non-existent window with ID {windowId}.")]
    private static partial void LogCloseMissingWindow(ILogger logger, ulong windowId);

    private void LogCloseMissingWindow(WindowId windowId)
        => LogCloseMissingWindow(this.logger, windowId.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Closing the window with ID {windowId}.")]
    private static partial void LogClosingWindow(ILogger logger, ulong windowId);

    [Conditional("DEBUG")]
    private void LogClosingWindow(WindowId windowId)
        => LogClosingWindow(this.logger, windowId.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to close the window with ID {windowId}.")]
    private static partial void LogCloseWindowFailed(ILogger logger, Exception exception, ulong windowId);

    private void LogCloseWindowFailed(Exception exception, WindowId windowId)
        => LogCloseWindowFailed(this.logger, exception, windowId.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "An attempt was made to activate a non-existent window with ID {windowId}.")]
    private static partial void LogActivateMissingWindow(ILogger logger, ulong windowId);

    private void LogActivateMissingWindow(WindowId windowId)
        => LogActivateMissingWindow(this.logger, windowId.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to activate the window with ID {windowId}.")]
    private static partial void LogActivateWindowFailed(ILogger logger, Exception exception, ulong windowId);

    private void LogActivateWindowFailed(Exception exception, WindowId windowId)
        => LogActivateWindowFailed(this.logger, exception, windowId.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Closing {count} windows.")]
    private static partial void LogClosingAllWindows(ILogger logger, int count);

    private void LogClosingAllWindows(int count)
        => LogClosingAllWindows(this.logger, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The window with ID {windowId} has been closed.")]
    private static partial void LogWindowClosed(ILogger logger, ulong windowId);

    private void LogWindowClosed(WindowId windowId)
        => LogWindowClosed(this.logger, windowId.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to apply the theme to the window with ID {windowId}.")]
    private static partial void LogThemeApplyFailed(ILogger logger, Exception exception, ulong windowId);

    private void LogThemeApplyFailed(Exception exception, WindowId windowId)
        => LogThemeApplyFailed(this.logger, exception, windowId.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The window with ID {windowId} and type {windowType} has been registered.")]
    private static partial void LogWindowRegistered(ILogger logger, ulong windowId, string windowType);

    private void LogWindowRegistered(WindowId windowId, WindowCategory category)
        => LogWindowRegistered(this.logger, windowId.Value, category.ToString());

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to register the window of type {windowType}.")]
    private static partial void LogRegisterWindowFailed(ILogger logger, Exception exception, string windowType);

    private void LogRegisterWindowFailed(Exception exception, string windowType)
        => LogRegisterWindowFailed(this.logger, exception, windowType);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Router integration has been enabled.")]
    private static partial void LogRouterIntegrationEnabled(ILogger logger);

    private void LogRouterIntegrationEnabled()
        => LogRouterIntegrationEnabled(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The router is not available; router integration has been disabled.")]
    private static partial void LogRouterNotAvailable(ILogger logger);

    [Conditional("DEBUG")]
    private void LogRouterNotAvailable()
        => LogRouterNotAvailable(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tracking the router window target {targetName}.")]
    private static partial void LogTrackingRouterWindow(ILogger logger, string targetName);

    [Conditional("DEBUG")]
    private void LogTrackingRouterWindow(string targetName)
        => LogTrackingRouterWindow(this.logger, targetName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "A router window is already being tracked for the target {targetName}.")]
    private static partial void LogRouterWindowAlreadyTracked(ILogger logger, string targetName);

    private void LogRouterWindowAlreadyTracked(string targetName)
        => LogRouterWindowAlreadyTracked(this.logger, targetName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to track the router window for the target {targetName}.")]
    private static partial void LogRouterWindowTrackingFailed(ILogger logger, Exception exception, string targetName);

    private void LogRouterWindowTrackingFailed(Exception exception, string targetName)
        => LogRouterWindowTrackingFailed(this.logger, exception, targetName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The router window for the target {targetName} has been destroyed.")]
    private static partial void LogRouterWindowDestroyed(ILogger logger, string targetName);

    [Conditional("DEBUG")]
    private void LogRouterWindowDestroyed(string targetName)
        => LogRouterWindowDestroyed(this.logger, targetName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The decoration was resolved explicitly for the window with ID {windowId}.")]
    private static partial void LogDecorationResolvedExplicit(ILogger logger, ulong windowId);

    private void LogDecorationResolvedExplicit(WindowId windowId)
        => LogDecorationResolvedExplicit(this.logger, windowId.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The decoration was resolved from settings for the window with ID {windowId} and category {category}.")]
    private static partial void LogDecorationResolvedFromSettings(ILogger logger, ulong windowId, string category);

    [Conditional("DEBUG")]
    private void LogDecorationResolvedFromSettings(WindowId windowId, WindowCategory category)
        => LogDecorationResolvedFromSettings(this.logger, windowId.Value, category.ToString());

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "No decoration was resolved for the window with ID {windowId} because no settings service is available.")]
    private static partial void LogNoDecorationResolved(ILogger logger, ulong windowId);

    [Conditional("DEBUG")]
    private void LogNoDecorationResolved(WindowId windowId)
        => LogNoDecorationResolved(this.logger, windowId.Value);
}
