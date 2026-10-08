// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.World.Documents;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>Structured scene-editor lifecycle logging.</summary>
public partial class SceneEditorViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Registering for SceneLoadedMessage for document {DocumentId}")]
    private static partial void LogRegisteringForSceneLoaded(ILogger logger, Guid documentId);

    private void LogRegisteringForSceneLoaded(Guid documentId)
        => LogRegisteringForSceneLoaded(this.logger, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Unregistering from messages for document {DocumentId}")]
    private static partial void LogUnregisteringFromMessages(ILogger logger, Guid documentId);

    private void LogUnregisteringFromMessages(Guid documentId)
        => LogUnregisteringFromMessages(this.logger, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Deferring layout change to {Layout} until scene ready for document {DocumentId}")]
    private static partial void LogDeferringLayoutChange(ILogger logger, SceneViewLayout layout, Guid? documentId);

    private void LogDeferringLayoutChange(SceneViewLayout layout)
        => LogDeferringLayoutChange(this.logger, layout, this.Metadata?.DocumentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Creating viewport VM (Index={Index}) Id={ViewportId} for document {DocumentId}")]
    private static partial void LogCreatingViewport(ILogger logger, int index, Guid viewportId, Guid? documentId);

    private void LogCreatingViewport(int index, ViewportViewModel viewport)
        => LogCreatingViewport(this.logger, index, viewport.ViewportId, this.Metadata?.DocumentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Save requested for document {DocumentId}")]
    private static partial void LogSaveRequested(ILogger logger, Guid? documentId);

    private void LogSaveRequested()
        => LogSaveRequested(this.logger, this.Metadata?.DocumentId);

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
    private static partial void LogBrowseToAssetRequested(ILogger logger, int assetCount, Guid? documentId);

    private void LogBrowseToAssetRequested(int assetCount)
        => LogBrowseToAssetRequested(this.logger, assetCount, this.Metadata?.DocumentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Quick Add {Item} to document {DocumentId}")]
    private static partial void LogQuickAddRequested(ILogger logger, string item, Guid? documentId);

    private void LogQuickAddRequested(string item)
        => LogQuickAddRequested(this.logger, item, this.Metadata?.DocumentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "SceneLoadedMessage received for document {DocumentId} ; restoring layout {Layout}")]
    private static partial void LogSceneLoadedReceived(ILogger logger, Guid? documentId, SceneViewLayout layout);

    private void LogSceneLoadedReceived(SceneViewLayout layout)
        => LogSceneLoadedReceived(this.logger, this.Metadata?.DocumentId, layout);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Viewport pane {Pane} of document {DocumentId} looked through camera {CameraId}, which no longer exists; it uses its editor camera")]
    private static partial void LogRestoredCameraMissing(ILogger logger, int pane, Guid? documentId, Guid cameraId);

    private void LogRestoredCameraMissing(int pane, Guid cameraId)
        => LogRestoredCameraMissing(this.logger, pane, this.Metadata?.DocumentId, cameraId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The runtime rejected the selection outline of document {DocumentId}; no engine session is running")]
    private static partial void LogSelectionOutlineRejected(ILogger logger, Guid? documentId);

    private void LogSelectionOutlineRejected()
        => LogSelectionOutlineRejected(this.logger, this.Metadata?.DocumentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The runtime rejected the transform gizmo of document {DocumentId}; no engine session is running")]
    private static partial void LogTransformGizmoRejected(ILogger logger, Guid? documentId);

    private void LogTransformGizmoRejected()
        => LogTransformGizmoRejected(this.logger, this.Metadata?.DocumentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The runtime rejected the scene helpers of document {DocumentId}; no engine session is running")]
    private static partial void LogSceneHelpersRejected(ILogger logger, Guid? documentId);

    private void LogSceneHelpersRejected()
        => LogSceneHelpersRejected(this.logger, this.Metadata?.DocumentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The runtime could not update the scene helpers of document {DocumentId}")]
    private static partial void LogSceneHelpersFailed(ILogger logger, Exception exception, Guid? documentId);

    private void LogSceneHelpersFailed(Exception exception)
        => LogSceneHelpersFailed(this.logger, exception, this.Metadata?.DocumentId);
}
