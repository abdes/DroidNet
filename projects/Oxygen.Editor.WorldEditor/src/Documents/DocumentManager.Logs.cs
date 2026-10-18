// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Documents;

#pragma warning disable SA1204 // Each log method keeps its static and instance forms together

/// <summary>
///     Logging helpers for <see cref="DocumentManager"/>.
/// </summary>
public partial class DocumentManager
{
    [LoggerMessage(Level = LogLevel.Information, Message = "Request to open scene '{SceneUri}' in window {WindowId} received")]
    private static partial void LogOnOpenSceneRequested(ILogger logger, AssetPath sceneUri, WindowId windowId);

    private void LogOnOpenSceneRequested(AssetPath sceneUri) => LogOnOpenSceneRequested(this.logger, sceneUri, this.windowId);

    [LoggerMessage(Level = LogLevel.Error, Message = "Cannot open scene '{SceneUri}': the window ID is invalid")]
    private partial void LogCannotOpenSceneWindowIdInvalid(AssetPath sceneUri);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Did not open scene '{Path}': {Reason}. The current scene stays open.")]
    private partial void LogSceneNotOpened(FileSystemPath path, string reason);

    [LoggerMessage(Level = LogLevel.Information, Message = "Opened scene `{SceneName}` ({SceneId}) in window {WindowId}")]
    private static partial void LogOpenedNewScene(ILogger logger, string sceneName, SceneId sceneId, WindowId windowId);

    private void LogOpenedNewScene(World.Scene scene)
        => LogOpenedNewScene(this.logger, scene.Name, scene.Id, this.windowId);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Did not open '{Path}': {Reason}.")]
    private partial void LogDocumentNotOpened(FileSystemPath path, string reason);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Failed to persist last opened scene {SceneName} for project {ProjectName}.")]
    private partial void LogSceneUsageUpdateFailed(Exception exception, string sceneName, string projectName);

    [LoggerMessage(Level = LogLevel.Error, Message = "Scene replacement failed for '{Path}'. Reload the previous saved scene if its document was retired.")]
    private partial void LogSceneReplacementFailed(Exception exception, FileSystemPath path);

    [LoggerMessage(Level = LogLevel.Information, Message = "Closed the document '{Document}' because its file '{Path}' was deleted in the editor.")]
    private partial void LogDeletedDocumentClosed(string document, FileSystemPath path);

    [LoggerMessage(Level = LogLevel.Warning, Message = "The document '{Document}' stays open although its file '{Path}' was deleted in the editor; saving it writes the file again.")]
    private partial void LogDeletedDocumentNotClosed(string document, FileSystemPath path);
}
