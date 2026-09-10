// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.World.SceneExplorer;

/// <summary>Reports scene authoring and loading failures.</summary>
public partial class SceneExplorerViewModel
{
    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to apply scene explorer add for {Item}.")]
    private partial void LogAuthoringAddFailed(Exception exception, string item);

    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to load scene {SceneId} ({SceneName}).")]
    private partial void LogAuthoringLoadFailed(Exception exception, Guid sceneId, string sceneName);

    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to apply in-place rename for {Item}.")]
    private partial void LogAuthoringRenameFailed(Exception exception, string item);
}
