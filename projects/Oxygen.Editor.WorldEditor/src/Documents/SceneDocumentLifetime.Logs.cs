// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.World.Documents;

/// <summary>
///     Logging helpers for <see cref="SceneDocumentLifetime"/>.
/// </summary>
public sealed partial class SceneDocumentLifetime
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Failed to persist last opened scene {SceneName} for project {ProjectName}.")]
    private static partial void LogSceneUsageUpdateFailed(ILogger logger, Exception exception, string sceneName, string projectName);

    private void LogSceneUsageUpdateFailed(Exception exception, string sceneName, string projectName)
        => LogSceneUsageUpdateFailed(this.logger, exception, sceneName, projectName);
}
