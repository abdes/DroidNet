// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Controls;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.World.SceneExplorer.Services;

/// <summary>
///     Logging helpers for <see cref="SceneExplorerService"/>.
/// </summary>
public partial class SceneExplorerService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "GetScene: Item={Item} SceneHash={SceneHash}")]
    private static partial void LogGetSceneFound(ILogger logger, string item, int sceneHash);

    [Conditional("DEBUG")]
    private void LogGetSceneFound(ITreeItem item, Scene scene)
        => LogGetSceneFound(this.logger, item?.Label ?? "<null>", scene?.GetHashCode() ?? 0);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "GetScene: Could not find scene for item {Item}")]
    private static partial void LogGetSceneNotFound(ILogger logger, string item);

    private void LogGetSceneNotFound(ITreeItem item)
        => LogGetSceneNotFound(this.logger, item?.Label ?? "<null>");
}
