// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.SceneExplorer.Operations;

/// <summary>
///     Logging helpers for <see cref="SceneMutator"/>.
/// </summary>
public partial class SceneMutator
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Added node '{NodeName}' to RootNodes")]
    private static partial void LogAddedNodeToRootNodes(ILogger logger, string nodeName);

    [Conditional("DEBUG")]
    private void LogAddedNodeToRootNodes(SceneNode node)
        => LogAddedNodeToRootNodes(this.logger, node?.Name ?? "<null>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Removed node '{NodeName}' from RootNodes after parenting")]
    private static partial void LogRemovedNodeFromRootNodesAfterParenting(ILogger logger, string nodeName);

    [Conditional("DEBUG")]
    private void LogRemovedNodeFromRootNodesAfterParenting(SceneNode node)
        => LogRemovedNodeFromRootNodesAfterParenting(this.logger, node?.Name ?? "<null>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Removed node '{NodeName}' from RootNodes")]
    private static partial void LogRemovedNodeFromRootNodes(ILogger logger, string nodeName);

    [Conditional("DEBUG")]
    private void LogRemovedNodeFromRootNodes(SceneNode node)
        => LogRemovedNodeFromRootNodes(this.logger, node?.Name ?? "<null>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Removed hierarchy root '{NodeName}' from RootNodes")]
    private static partial void LogRemovedHierarchyRootFromRootNodes(ILogger logger, string nodeName);

    [Conditional("DEBUG")]
    private void LogRemovedHierarchyRootFromRootNodes(SceneNode node)
        => LogRemovedHierarchyRootFromRootNodes(this.logger, node?.Name ?? "<null>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Moved node '{NodeName}' to RootNodes")]
    private static partial void LogMovedNodeToRootNodes(ILogger logger, string nodeName);

    [Conditional("DEBUG")]
    private void LogMovedNodeToRootNodes(SceneNode node)
        => LogMovedNodeToRootNodes(this.logger, node?.Name ?? "<null>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Removed node '{NodeName}' from RootNodes after reparenting")]
    private static partial void LogRemovedNodeFromRootNodesAfterReparenting(ILogger logger, string nodeName);

    [Conditional("DEBUG")]
    private void LogRemovedNodeFromRootNodesAfterReparenting(SceneNode node)
        => LogRemovedNodeFromRootNodesAfterReparenting(this.logger, node?.Name ?? "<null>");
}
