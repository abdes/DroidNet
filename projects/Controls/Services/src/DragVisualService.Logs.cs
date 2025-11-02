// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls;

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
            this.hotspot.X,
            this.hotspot.Y);
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
        Message = "Position update: Token={Token}, ScreenX={ScreenX}, ScreenY={ScreenY}, DPI={Dpi}, PhysicalX={PhysicalX}, PhysicalY={PhysicalY}, WindowSize={WindowWidth}x{WindowHeight}")]
    private static partial void LogPositionUpdated(
        ILogger logger,
        Guid token,
        int screenX,
        int screenY,
        uint dpi,
        int physicalX,
        int physicalY,
        int windowWidth,
        int windowHeight);

    private void LogPositionUpdated(Windows.Foundation.Point screenPoint, uint dpi, Native.POINT physicalPos)
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
            physicalPos.X,
            physicalPos.Y,
            this.overlayWidth,
            this.overlayHeight);
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
        Message = "Layered window created: Size={Width}x{Height} (physical), DPI=96 (default)")]
    private static partial void LogLayeredWindowCreated(ILogger logger, int width, int height);

    private void LogLayeredWindowCreated()
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        LogLayeredWindowCreated(this.logger, this.overlayWidth, this.overlayHeight);
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
        Message = "DIB section created: Size={Width}x{Height} physical pixels, Format=32-bit BGRA")]
    private static partial void LogDibSectionCreated(ILogger logger, int width, int height);

    private void LogDibSectionCreated()
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        LogDibSectionCreated(this.logger, this.overlayWidth, this.overlayHeight);
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
        Message = "Layered window destroyed: Size was {Width}x{Height}")]
    private static partial void LogLayeredWindowDestroyed(ILogger logger, int width, int height);

    private void LogLayeredWindowDestroyed()
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        LogLayeredWindowDestroyed(this.logger, this.overlayWidth, this.overlayHeight);
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
