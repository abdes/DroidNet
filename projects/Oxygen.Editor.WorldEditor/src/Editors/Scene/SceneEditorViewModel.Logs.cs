// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.Documents;
using Oxygen.Interop;
using Oxygen.Editor.WorldEditor.Controls;

namespace Oxygen.Editor.WorldEditor.Editors.Scene;

public partial class SceneEditorViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Registering for SceneLoadedMessage for document {DocumentId}")]
    private static partial void LogRegisteringForSceneLoaded(ILogger logger, Guid? documentId);

    private void LogRegisteringForSceneLoaded()
        => LogRegisteringForSceneLoaded(this.logger, this.Metadata?.DocumentId);

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
        Message = "Creating viewport VM (Index={Index}) Id={ViewportId} for document {DocumentId} with ClearColor={R},{G},{B},{A}")]
    private static partial void LogCreatingViewport(ILogger logger, int index, Guid viewportId, Guid? documentId, float r, float g, float b, float a);

    private void LogCreatingViewport(int index, Guid viewportId, ColorManaged color)
        => LogCreatingViewport(this.logger, index, viewportId, this.Metadata?.DocumentId, color.R, color.G, color.B, color.A);

    private void LogCreatingViewport(int index, ViewportViewModel viewport)
        => LogCreatingViewport(index, viewport.ViewportId, viewport.ClearColor);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Failed to set engine target FPS: {Message}")]
    private static partial void LogFailedToSetEngineTargetFps(ILogger logger, string message, Exception ex);

    private void LogFailedToSetEngineTargetFps(Exception ex)
        => LogFailedToSetEngineTargetFps(this.logger, ex.Message, ex);

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
        Message = "Locate-in-content-browser requested for document {DocumentId}")]
    private static partial void LogLocateInContentBrowserRequested(ILogger logger, Guid? documentId);

    private void LogLocateInContentBrowserRequested()
        => LogLocateInContentBrowserRequested(this.logger, this.Metadata?.DocumentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Request to add primitive {Kind} to document {DocumentId}")]
    private static partial void LogRequestToAddPrimitive(ILogger logger, string kind, Guid? documentId);

    private void LogRequestToAddPrimitive(string kind)
        => LogRequestToAddPrimitive(this.logger, kind, this.Metadata?.DocumentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Request to add light {Kind} to document {DocumentId}")]
    private static partial void LogRequestToAddLight(ILogger logger, string kind, Guid? documentId);

    private void LogRequestToAddLight(string kind)
        => LogRequestToAddLight(this.logger, kind, this.Metadata?.DocumentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "SceneLoadedMessage received for document {DocumentId} ; restoring layout {Layout}")]
    private static partial void LogSceneLoadedReceived(ILogger logger, Guid? documentId, SceneViewLayout layout);

    private void LogSceneLoadedReceived(SceneViewLayout layout)
        => LogSceneLoadedReceived(this.logger, this.Metadata?.DocumentId, layout);
}
