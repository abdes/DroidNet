// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Workspace;

/// <summary>Logging helpers for <see cref="WorkspaceViewModel"/>.</summary>
public partial class WorkspaceViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Cannot open initial scene {SceneName}: the window has no document service.")]
    private static partial void LogInitialSceneManagerUnavailable(ILogger logger, string? sceneName);

    private void LogInitialSceneManagerUnavailable(AssetPath scene)
    {
        if (!this.logger.IsEnabled(LogLevel.Warning))
        {
            return;
        }

        var sceneName = scene.ToString();
        LogInitialSceneManagerUnavailable(this.logger, sceneName);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Initial scene {SceneName} was selected for project {ProjectName}, but the document did not open.")]
    private static partial void LogInitialSceneNotOpened(ILogger logger, string? sceneName, string? projectName);

    private void LogInitialSceneNotOpened(AssetPath scene, string? projectName)
    {
        if (!this.logger.IsEnabled(LogLevel.Warning))
        {
            return;
        }

        var sceneName = scene.ToString();
        LogInitialSceneNotOpened(this.logger, sceneName, projectName);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Failed to restore the last opened scene for project {ProjectName}.")]
    private static partial void LogSceneRestorationFailed(ILogger logger, Exception exception, string? projectName);

    private void LogSceneRestorationFailed(Exception exception, string? projectName)
        => LogSceneRestorationFailed(this.logger, exception, projectName);
}
