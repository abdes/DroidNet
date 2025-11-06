// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Aura.Drag;

/// <summary>
///     Implementation of <see cref="IDragVisualService"/> using a Win32 layered window for the drag
///     overlay. The overlay is topmost, non-activating, click-through, and survives source
///     AppWindow closure.
/// </summary>
public partial class DragVisualService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "{ServiceName} created.")]
    private static partial void LogCreated(ILogger logger, string serviceName);

    private void LogCreated() => LogCreated(this.logger, nameof(DragVisualService));

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Session started: Token={Token}, Size={DescriptorWidth}x{DescriptorHeight}, Hotspot=({HotspotX}, {HotspotY})")]
    private static partial void LogSessionStarted(
        ILogger logger,
        Guid token,
        double descriptorWidth,
        double descriptorHeight,
        double hotspotX,
        double hotspotY);

    private void LogSessionStarted()
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var size = this.activeDescriptor?.RequestedSize ?? new Windows.Foundation.Size(0, 0);
        LogSessionStarted(
            this.logger,
            this.activeToken?.Id ?? Guid.Empty,
            size.Width,
            size.Height,
            this.hotspot.Point.X,
            this.hotspot.Point.Y);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Session ended: Token={Token}")]
    private static partial void LogSessionEnded(ILogger logger, Guid token);

    private void LogSessionEnded()
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        LogSessionEnded(this.logger, this.activeToken?.Id ?? Guid.Empty);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Position update: Token={Token}, ScreenX={ScreenX}, ScreenY={ScreenY}, DPI={Dpi}, PhysicalX={PhysicalX}, PhysicalY={PhysicalY}")]
    private static partial void LogPositionUpdated(
        ILogger logger,
        Guid token,
        int screenX,
        int screenY,
        uint dpi,
        int physicalX,
        int physicalY);

    private void LogPositionUpdated(Windows.Foundation.Point screenPoint, uint dpi, Windows.Foundation.Point physicalPos)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        LogPositionUpdated(
            this.logger,
            this.activeToken?.Id ?? Guid.Empty,
            (int)screenPoint.X,
            (int)screenPoint.Y,
            dpi,
            (int)Math.Round(physicalPos.X),
            (int)Math.Round(physicalPos.Y));
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Attempt to start session failed: Session already active. Existing Token={ExistingToken}")]
    private static partial void LogSessionAlreadyActive(ILogger logger, Guid existingToken);

    private void LogSessionAlreadyActive()
    {
        LogSessionAlreadyActive(this.logger, this.activeToken?.Id ?? Guid.Empty);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Token mismatch in UpdatePosition: Expected={ExpectedToken}, Provided={ProvidedToken}")]
    private static partial void LogTokenMismatchUpdatePosition(ILogger logger, Guid expectedToken, Guid providedToken);

    private void LogTokenMismatchUpdatePosition(DragSessionToken token)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        LogTokenMismatchUpdatePosition(this.logger, this.activeToken?.Id ?? Guid.Empty, token.Id);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Overlay window not initialized during UpdatePosition")]
    private static partial void LogOverlayWindowNotInitialized(ILogger logger);

    private void LogOverlayWindowNotInitialized()
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        LogOverlayWindowNotInitialized(this.logger);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Layered window created")]
    private static partial void LogLayeredWindowCreated(ILogger logger);

    private void LogLayeredWindowCreated()
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        LogLayeredWindowCreated(this.logger);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to create layered window. Win32 Error={ErrorCode}")]
    private static partial void LogCreateLayeredWindowFailed(ILogger logger, int errorCode);

    private void LogCreateLayeredWindowFailed(int errorCode)
    {
        LogCreateLayeredWindowFailed(this.logger, errorCode);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "DIB section created (content updated)")]
    private static partial void LogDibSectionCreated(ILogger logger);

    private void LogDibSectionCreated()
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        LogDibSectionCreated(this.logger);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to create compatible DC for DIB section")]
    private static partial void LogCreateCompatibleDcFailed(ILogger logger);

    private void LogCreateCompatibleDcFailed()
    {
        LogCreateCompatibleDcFailed(this.logger);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to create DIB section")]
    private static partial void LogCreateDibSectionFailed(ILogger logger);

    private void LogCreateDibSectionFailed()
    {
        LogCreateDibSectionFailed(this.logger);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Layered window destroyed")]
    private static partial void LogLayeredWindowDestroyed(ILogger logger);

    private void LogLayeredWindowDestroyed()
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        LogLayeredWindowDestroyed(this.logger);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Descriptor property changed: PropertyName={PropertyName}, Action={Action}")]
    private static partial void LogDescriptorPropertyChanged(ILogger logger, string propertyName, string action);

    private void LogDescriptorPropertyChanged(string propertyName, string action)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        LogDescriptorPropertyChanged(this.logger, propertyName, action);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Attempted to end session with mismatched token. Active={ActiveToken}, Provided={ProvidedToken}")]
    private static partial void LogTokenMismatchEndSession(ILogger logger, Guid activeToken, Guid providedToken);

    private void LogTokenMismatchEndSession(DragSessionToken token)
    {
        LogTokenMismatchEndSession(this.logger, this.activeToken?.Id ?? Guid.Empty, token.Id);
    }
}
