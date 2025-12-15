// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.UI;

namespace Oxygen.Editor.WorldEditor.Editors;

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
}
