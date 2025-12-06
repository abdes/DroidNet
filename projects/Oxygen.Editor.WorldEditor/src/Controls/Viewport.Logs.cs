// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Oxygen.Interop;

namespace Oxygen.Editor.WorldEditor.Controls;

#pragma warning disable SA1204 // Static elements should appear before instance elements

/// <summary>
///     Logging helpers for <see cref="Viewport"/>.
/// </summary>
public sealed partial class Viewport
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[Viewport] Loaded invoked. activeLoadCount={ActiveLoadCount}")]
    private static partial void LogLoadedInvoked(ILogger logger, int activeLoadCount);

    [Conditional("DEBUG")]
    private void LogLoadedInvoked(int activeLoadCount)
        => LogLoadedInvoked(this.logger, activeLoadCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[Viewport] Load count changed. newValue={ActiveLoadCount}")]
    private static partial void LogLoadCountChanged(ILogger logger, int activeLoadCount);

    [Conditional("DEBUG")]
    private void LogLoadCountChanged(int activeLoadCount)
        => LogLoadCountChanged(this.logger, activeLoadCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[Viewport] Surface already attached on load; requesting resize only.")]
    private static partial void LogSurfaceAlreadyAttachedOnLoad(ILogger logger);

    [Conditional("DEBUG")]
    private void LogSurfaceAlreadyAttachedOnLoad()
        => LogSurfaceAlreadyAttachedOnLoad(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[Viewport] SwapChainPanel size hook registered.")]
    private static partial void LogSwapChainHookRegistered(ILogger logger);

    [Conditional("DEBUG")]
    private void LogSwapChainHookRegistered()
        => LogSwapChainHookRegistered(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[Viewport] SwapChainPanel size hook unregistered.")]
    private static partial void LogSwapChainHookUnregistered(ILogger logger);

    [Conditional("DEBUG")]
    private void LogSwapChainHookUnregistered()
        => LogSwapChainHookUnregistered(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[Viewport] SwapChainPanel size changed to {Width}x{Height}.")]
    private static partial void LogSwapChainSizeChanged(ILogger logger, double width, double height);

    [Conditional("DEBUG")]
    private void LogSwapChainSizeChanged(double width, double height)
        => LogSwapChainSizeChanged(this.logger, width, height);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[Viewport] Attach requested. reason={Reason}")]
    private static partial void LogAttachRequested(ILogger logger, string reason);

    // CompositionScaleChanged logging removed; transform is now static in XAML.

    [Conditional("DEBUG")]
    private void LogAttachRequested(string reason)
        => LogAttachRequested(this.logger, reason);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[Viewport] Resize skipped. reason={Reason}")]
    private static partial void LogResizeSkipped(ILogger logger, string reason);

    [Conditional("DEBUG")]
    private void LogResizeSkipped(string reason)
        => LogResizeSkipped(this.logger, reason);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[Viewport] Resizing surface to {Width}x{Height} pixels.")]
    private static partial void LogViewportResizing(ILogger logger, uint width, uint height);

    [Conditional("DEBUG")]
    private void LogViewportResizing(uint width, uint height)
        => LogViewportResizing(this.logger, width, height);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[Viewport] Resize completed for {Width}x{Height} pixels.")]
    private static partial void LogViewportResized(ILogger logger, uint width, uint height);

    [Conditional("DEBUG")]
    private void LogViewportResized(uint width, uint height)
        => LogViewportResized(this.logger, width, height);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "[Viewport] Failed to resize viewport surface.")]
    private static partial void LogResizeFailed(ILogger logger, Exception exception);

    private void LogResizeFailed(Exception exception)
        => LogResizeFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[Viewport] Unloaded invoked. activeLoadCount={ActiveLoadCount}")]
    private static partial void LogUnloadedInvoked(ILogger logger, int activeLoadCount);

    [Conditional("DEBUG")]
    private void LogUnloadedInvoked(int activeLoadCount)
        => LogUnloadedInvoked(this.logger, activeLoadCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[Viewport] Unload ignored because active references remain. count={ActiveLoadCount}")]
    private static partial void LogUnloadIgnored(ILogger logger, int activeLoadCount);

    [Conditional("DEBUG")]
    private void LogUnloadIgnored(int activeLoadCount)
        => LogUnloadIgnored(this.logger, activeLoadCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[Viewport] Detach requested. reason={Reason}")]
    private static partial void LogDetachRequested(ILogger logger, string reason);

    [Conditional("DEBUG")]
    private void LogDetachRequested(string reason)
        => LogDetachRequested(this.logger, reason);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "[Viewport] ViewModel does not expose an engine service; attachment skipped.")]
    private static partial void LogMissingEngineService(ILogger logger);

    private void LogMissingEngineService()
        => LogMissingEngineService(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "[Viewport] SwapChainPanel not ready; attachment skipped.")]
    private static partial void LogSwapChainPanelNotReady(ILogger logger);

    private void LogSwapChainPanelNotReady()
        => LogSwapChainPanelNotReady(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[Viewport] Surface attached for viewportId={ViewportId}.")]
    private static partial void LogSurfaceAttached(ILogger logger, Guid viewportId);

    [Conditional("DEBUG")]
    private void LogSurfaceAttached(Guid viewportId)
        => LogSurfaceAttached(this.logger, viewportId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[Viewport] Attachment canceled. reason={Reason}")]
    private static partial void LogAttachmentCanceled(ILogger logger, string reason);

    [Conditional("DEBUG")]
    private void LogAttachmentCanceled(string reason)
        => LogAttachmentCanceled(this.logger, reason);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "[Viewport] Failed to attach viewport surface.")]
    private static partial void LogAttachmentFailed(ILogger logger, Exception exception);

    private void LogAttachmentFailed(Exception exception)
        => LogAttachmentFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[Viewport] Surface lease disposed. viewportId={ViewportId}")]
    private static partial void LogLeaseDisposed(ILogger logger, string viewportId);

    [Conditional("DEBUG")]
    private void LogLeaseDisposed(string viewportId)
        => LogLeaseDisposed(this.logger, viewportId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "[Viewport] Failed to dispose viewport surface lease.")]
    private static partial void LogLeaseDisposeFailed(ILogger logger, Exception exception);

    private void LogLeaseDisposeFailed(Exception exception)
        => LogLeaseDisposeFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "[Viewport] Created engine view for viewport={ViewportId} viewId={ViewId}")]
    private static partial void LogViewCreated(ILogger logger, Guid viewportId, string viewId);

    private void LogViewCreated(Guid viewportId, ViewIdManaged viewId)
        => LogViewCreated(this.logger, viewportId, viewId.ToString());

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "[Viewport] Destroyed engine view for viewport={ViewportId}")]
    private static partial void LogViewDestroyed(ILogger logger, Guid viewportId);

    private void LogViewDestroyed(Guid viewportId)
        => LogViewDestroyed(this.logger, viewportId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "[Viewport] Create view failed for viewport={ViewportId}")]
    private static partial void LogCreateViewFailed(ILogger logger, Guid viewportId, Exception ex);

    private void LogCreateViewFailed(Guid viewportId, Exception ex)
        => LogCreateViewFailed(this.logger, viewportId, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "[Viewport] Destroy view failed for viewport={ViewportId}")]
    private static partial void LogDestroyViewFailed(ILogger logger, Guid viewportId, Exception ex);

    private void LogDestroyViewFailed(Guid viewportId, Exception ex)
        => LogDestroyViewFailed(this.logger, viewportId, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[Viewport] ViewModel changed from {PreviousViewportId} to {CurrentViewportId}.")]
    private static partial void LogViewModelChanged(ILogger logger, string previousViewportId, string currentViewportId);

    [Conditional("DEBUG")]
    private void LogViewModelChanged(string previousViewportId, string currentViewportId)
        => LogViewModelChanged(this.logger, previousViewportId, currentViewportId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[Viewport] Attach outcome ignored because {Reason}.")]
    private static partial void LogAttachOutcomeIgnored(ILogger logger, string reason);

    [Conditional("DEBUG")]
    private void LogAttachOutcomeIgnored(string reason)
        => LogAttachOutcomeIgnored(this.logger, reason);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "[Viewport] SwapChainPanel access failed during resize.")]
    private static partial void LogSwapChainAccessFailed(ILogger logger, Exception exception);

    private void LogSwapChainAccessFailed(Exception exception)
        => LogSwapChainAccessFailed(this.logger, exception);
}

#pragma warning restore SA1204
