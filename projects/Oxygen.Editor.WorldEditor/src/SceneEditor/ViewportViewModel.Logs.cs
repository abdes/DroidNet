// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.LevelEditor;

/// <summary>
/// Logging helpers for <see cref="ViewportViewModel"/>.
/// </summary>
public partial class ViewportViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ViewportViewModel initialized. DocumentId: {DocumentId}")]
    private static partial void LogInitialized(ILogger logger, Guid documentId);

    [Conditional("DEBUG")]
    private void LogInitialized()
        => LogInitialized(this.logger, this.DocumentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Created engine view {ViewId} for viewport {ViewportId}")]
    private static partial void LogViewCreated(ILogger logger, Guid viewportId, ulong viewId);

    private void LogViewCreated(RuntimeViewId viewId)
        => LogViewCreated(this.logger, this.ViewportId, viewId.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Destroyed the engine view of viewport {ViewportId}")]
    private static partial void LogViewDestroyed(ILogger logger, Guid viewportId);

    private void LogViewDestroyed()
        => LogViewDestroyed(this.logger, this.ViewportId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Create view failed for viewport {ViewportId}")]
    private static partial void LogCreateViewFailed(ILogger logger, Guid viewportId, Exception ex);

    private void LogCreateViewFailed(Exception ex)
        => LogCreateViewFailed(this.logger, this.ViewportId, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Destroy view failed for viewport {ViewportId}")]
    private static partial void LogDestroyViewFailed(ILogger logger, Guid viewportId, Exception ex);

    private void LogDestroyViewFailed(Exception ex)
        => LogDestroyViewFailed(this.logger, this.ViewportId, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Viewport {ViewportId} kept its previous editor camera state: the view could not report it")]
    private static partial void LogEditorCameraUnavailable(ILogger logger, Guid viewportId, Exception? ex);

    private void LogEditorCameraUnavailable(Exception? ex = null)
        => LogEditorCameraUnavailable(this.logger, this.ViewportId, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Camera preview inset update failed for viewport {ViewportId}")]
    private static partial void LogInsetFailed(ILogger logger, Guid viewportId, Exception ex);

    private void LogInsetFailed(Exception ex)
        => LogInsetFailed(this.logger, this.ViewportId, ex);
}
