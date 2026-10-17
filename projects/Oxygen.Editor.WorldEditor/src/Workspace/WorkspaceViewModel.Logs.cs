// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.World.Workspace;

/// <summary>Source-generated diagnostics for workspace activation.</summary>
public partial class WorkspaceViewModel
{
    [LoggerMessage(Level = LogLevel.Warning, Message = "Cannot open initial scene {SceneName}: document manager is not available.")]
    private partial void LogInitialSceneManagerUnavailable(string? sceneName);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Initial scene {SceneName} was selected for project {ProjectName}, but the document did not open.")]
    private partial void LogInitialSceneNotOpened(string? sceneName, string? projectName);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Failed to restore the last opened scene for project {ProjectName}.")]
    private partial void LogSceneRestorationFailed(Exception exception, string? projectName);
}
