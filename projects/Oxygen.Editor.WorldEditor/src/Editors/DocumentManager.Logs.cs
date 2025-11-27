// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.WorldEditor.Editors;

/// <summary>
///     Logging helpers for <see cref="DocumentManager"/>.
/// </summary>
public partial class DocumentManager
{

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Request to open scene `{SceneName}` ({SceneId}) received")]
    private static partial void LogOnOpenSceneRequested(ILogger logger, string sceneName, Guid sceneId);

    private void LogOnOpenSceneRequested(string sceneName, Guid sceneId)
        => LogOnOpenSceneRequested(this.logger, sceneName, sceneId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Cannot open scene, window ID is invalid")]
    private static partial void LogCannotOpenSceneWindowIdInvalid(ILogger logger);

    private void LogCannotOpenSceneWindowIdInvalid()
        => LogCannotOpenSceneWindowIdInvalid(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Scene `{SceneName}` ({SceneId}) is already open, reactivating it")]
    private static partial void LogReactivatingExistingScene(ILogger logger, string sceneName, Guid sceneId);

    private void LogReactivatingExistingScene(string sceneName, Guid sceneId)
        => LogReactivatingExistingScene(this.logger, sceneName, sceneId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Scene `{SceneName}` ({SceneId}) reactivated")]
    private static partial void LogReactivatedExistingScene(ILogger logger, string sceneName, Guid sceneId);

    private void LogReactivatedExistingScene(string sceneName, Guid sceneId)
        => LogReactivatedExistingScene(this.logger, sceneName, sceneId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Opened new scene `{SceneName}` ({SceneId})")]
    private static partial void LogOpenedNewScene(ILogger logger, string sceneName, Guid sceneId);

    private void LogOpenedNewScene(string sceneName, Guid sceneId)
        => LogOpenedNewScene(this.logger, sceneName, sceneId);
}
