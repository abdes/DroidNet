// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Oxygen.Editor.ContentBrowser.Messages;

namespace Oxygen.Editor.World.Documents;

#pragma warning disable SA1204 // Each log method keeps its static and instance forms together

/// <summary>
///     Logging helpers for <see cref="DocumentManager"/>.
/// </summary>
public partial class DocumentManager
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Request to open scene `{SceneName}` ({SceneId}) in window {WindowId} received")]
    private static partial void LogOnOpenSceneRequested(ILogger logger, string sceneName, Guid sceneId, WindowId windowId);

    private void LogOnOpenSceneRequested(World.Scene scene)
        => LogOnOpenSceneRequested(this.logger, scene.Name, scene.Id, this.windowId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Cannot open scene `{SceneName}` ({SceneId}), window ID is invalid")]
    private static partial void LogCannotOpenSceneWindowIdInvalid(ILogger logger, string sceneName, Guid sceneId);

    private void LogCannotOpenSceneWindowIdInvalid(World.Scene scene)
        => LogCannotOpenSceneWindowIdInvalid(this.logger, scene.Name, scene.Id);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Scene `{SceneName}` ({SceneId}) is already open in window {WindowId}, reactivating it")]
    private static partial void LogReactivatingExistingScene(ILogger logger, string sceneName, Guid sceneId, WindowId windowId);

    [Conditional("DEBUG")]
    private void LogReactivatingExistingScene(World.Scene scene)
        => LogReactivatingExistingScene(this.logger, scene.Name, scene.Id, this.windowId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Scene `{SceneName}` ({SceneId}) reactivated for windopw {WindowId}")]
    private static partial void LogReactivatedExistingScene(ILogger logger, string sceneName, Guid sceneId, WindowId windowId);

    private void LogReactivatedExistingScene(World.Scene scene)
        => LogReactivatedExistingScene(this.logger, scene.Name, scene.Id, this.windowId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Opened new scene `{SceneName}` ({SceneId}) in window {WindowId}")]
    private static partial void LogOpenedNewScene(ILogger logger, string sceneName, Guid sceneId, WindowId windowId);

    private void LogOpenedNewScene(World.Scene scene)
        => LogOpenedNewScene(this.logger, scene.Name, scene.Id, this.windowId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to reactivate existing scene '{SceneName}' ({SceneId}) for window {WindowId}")]
    private static partial void LogSceneReactivationError(ILogger logger, string sceneName, Guid sceneId, WindowId windowId);

    private void LogSceneReactivationError(World.Scene scene)
        => LogSceneReactivationError(this.logger, scene.Name, scene.Id, this.windowId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Opening scene '{SceneName}' ({SceneId}) for window {WindowId} was aborted")]
    private static partial void LogSceneOpeningAborted(ILogger logger, string sceneName, Guid sceneId, WindowId windowId);

    private void LogSceneOpeningAborted(World.Scene scene)
        => LogSceneOpeningAborted(this.logger, scene.Name, scene.Id, this.windowId);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Failed to create material {MaterialUri}.")]
    private partial void LogMaterialCreationFailed(Exception exception, Uri materialUri);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Failed to persist last opened scene {SceneName} for project {ProjectName}.")]
    private partial void LogSceneUsageUpdateFailed(Exception exception, string sceneName, string projectName);

    [LoggerMessage(Level = LogLevel.Error, Message = "Scene replacement failed for {SceneId}. Reload the previous saved scene if its document was retired.")]
    private partial void LogSceneReplacementFailed(Exception exception, Guid sceneId);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Scene {Command} of '{Scene}' not done: {Reason}")]
    private partial void LogSceneCommandRejected(SceneAssetCommand command, string scene, string reason);

    [LoggerMessage(Level = LogLevel.Information, Message = "Renamed saved scene '{Previous}' to '{Name}'.")]
    private partial void LogSavedSceneRenamed(string previous, string name);

    [LoggerMessage(Level = LogLevel.Information, Message = "Duplicated saved scene '{Scene}' as '{Name}'.")]
    private partial void LogSceneDuplicated(string scene, string name);

    [LoggerMessage(Level = LogLevel.Information, Message = "Deleted scene '{Scene}': moved '{Path}' to the Recycle Bin and removed it from the project.")]
    private partial void LogSceneDeleted(string scene, string path);

    [LoggerMessage(Level = LogLevel.Information, Message = "Closed the material document '{Material}' because its file was deleted.")]
    private partial void LogDeletedMaterialClosed(Uri material);

    [LoggerMessage(Level = LogLevel.Warning, Message = "The material document '{Material}' stays open although its file was deleted; saving it recreates the file.")]
    private partial void LogDeletedMaterialNotClosed(Uri material);
}
