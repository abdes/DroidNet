// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneEditor;

/// <summary>Structured scene-editor lifecycle logging.</summary>
public partial class SceneEditorViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Deferring layout change to {Layout} until scene ready for document {DocumentId}")]
    private static partial void LogDeferringLayoutChange(ILogger logger, SceneViewLayout layout, DocumentId documentId);

    [Conditional("DEBUG")]
    private void LogDeferringLayoutChange(SceneViewLayout layout)
        => LogDeferringLayoutChange(this.logger, layout, this.Document.Id);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Creating viewport VM (Index={Index}) Id={ViewportId} for document {DocumentId}")]
    private static partial void LogCreatingViewport(ILogger logger, int index, Guid viewportId, DocumentId documentId);

    private void LogCreatingViewport(int index, ViewportViewModel viewport)
        => LogCreatingViewport(this.logger, index, viewport.ViewportId, this.Document.Id);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Save requested for document {DocumentId}")]
    private static partial void LogSaveRequested(ILogger logger, DocumentId documentId);

    private void LogSaveRequested()
        => LogSaveRequested(this.logger, this.Document.Id);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Save successful.")]
    private static partial void LogSaveSuccessful(ILogger logger);

    [Conditional("DEBUG")]
    private void LogSaveSuccessful()
        => LogSaveSuccessful(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Save failed.")]
    private static partial void LogSaveFailed(ILogger logger);

    [Conditional("DEBUG")]
    private void LogSaveFailed()
        => LogSaveFailed(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Save requested, but scene is not ready yet.")]
    private static partial void LogSaveRequestedButSceneNotReady(ILogger logger);

    [Conditional("DEBUG")]
    private void LogSaveRequestedButSceneNotReady()
        => LogSaveRequestedButSceneNotReady(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Browse to {AssetCount} asset(s) of the selection in document {DocumentId}")]
    private static partial void LogBrowseToAssetRequested(ILogger logger, int assetCount, DocumentId documentId);

    private void LogBrowseToAssetRequested(int assetCount)
        => LogBrowseToAssetRequested(this.logger, assetCount, this.Document.Id);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Quick Add {Item} to document {DocumentId}")]
    private static partial void LogQuickAddRequested(ILogger logger, string item, DocumentId documentId);

    private void LogQuickAddRequested(string item)
        => LogQuickAddRequested(this.logger, item, this.Document.Id);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The engine shows the scene of document {DocumentId}; restoring layout {Layout}")]
    private static partial void LogSceneLoadedReceived(ILogger logger, DocumentId documentId, SceneViewLayout layout);

    private void LogSceneLoadedReceived(SceneViewLayout layout)
        => LogSceneLoadedReceived(this.logger, this.Document.Id, layout);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Viewport pane {Pane} of document {DocumentId} looked through camera {CameraId}, which no longer exists; it uses its editor camera")]
    private static partial void LogRestoredCameraMissing(ILogger logger, int pane, DocumentId documentId, NodeId cameraId);

    private void LogRestoredCameraMissing(int pane, NodeId cameraId)
        => LogRestoredCameraMissing(this.logger, pane, this.Document.Id, cameraId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The runtime rejected the selection outline of document {DocumentId}; no engine session is running")]
    private static partial void LogSelectionOutlineRejected(ILogger logger, DocumentId documentId);

    [Conditional("DEBUG")]
    private void LogSelectionOutlineRejected()
        => LogSelectionOutlineRejected(this.logger, this.Document.Id);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The runtime rejected the transform gizmo of document {DocumentId}; no engine session is running")]
    private static partial void LogTransformGizmoRejected(ILogger logger, DocumentId documentId);

    [Conditional("DEBUG")]
    private void LogTransformGizmoRejected()
        => LogTransformGizmoRejected(this.logger, this.Document.Id);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The runtime rejected the scene helpers of document {DocumentId}; no engine session is running")]
    private static partial void LogSceneHelpersRejected(ILogger logger, DocumentId documentId);

    [Conditional("DEBUG")]
    private void LogSceneHelpersRejected()
        => LogSceneHelpersRejected(this.logger, this.Document.Id);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The runtime could not update the scene helpers of document {DocumentId}")]
    private static partial void LogSceneHelpersFailed(ILogger logger, Exception exception, DocumentId documentId);

    private void LogSceneHelpersFailed(Exception exception)
        => LogSceneHelpersFailed(this.logger, exception, this.Document.Id);
}
