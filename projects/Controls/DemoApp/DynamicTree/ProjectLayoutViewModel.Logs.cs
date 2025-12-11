// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace DroidNet.Controls.Demo.DynamicTree;

/// <summary>
///     Logging partial methods for <see cref="ProjectLayoutViewModel" />.
/// </summary>
public partial class ProjectLayoutViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "OperationError changed: {OperationError}")]
    private static partial void LogOperationErrorChanged(ILogger logger, string operationError);

    private void LogOperationErrorChanged(string? operationError)
        => LogOperationErrorChanged(this.logger, string.IsNullOrEmpty(operationError) ? "(null)" : operationError);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Item added: `{ItemName}`")]
    private static partial void LogItemAdded(ILogger logger, string itemName);

    private void LogItemAdded(string itemName)
        => LogItemAdded(this.logger, itemName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Scene add rejected. Parent is not a ProjectAdapter: {ParentType}")]
    private static partial void LogSceneAddRejectedParentNotProject(ILogger logger, string parentType);

    private void LogSceneAddRejectedParentNotProject(object? parent)
        => LogSceneAddRejectedParentNotProject(this.logger, parent?.ToString() ?? "<null>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Entity add rejected. Parent is not a SceneAdapter or EntityAdapter: {ParentType}")]
    private static partial void LogEntityAddRejectedParentType(ILogger logger, string parentType);

    private void LogEntityAddRejectedParentType(object? parent)
        => LogEntityAddRejectedParentType(this.logger, parent?.ToString() ?? "<null>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Entity add rejected. Parent does not accept children.")]
    private static partial void LogEntityAddRejectedParentDoesNotAcceptChildren(ILogger logger);

    private void LogEntityAddRejectedParentDoesNotAcceptChildren()
        => LogEntityAddRejectedParentDoesNotAcceptChildren(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Remove rejected: item is locked: {ItemLabel}")]
    private static partial void LogRemoveRejectedItemIsLocked(ILogger logger, string itemLabel);

    private void LogRemoveRejectedItemIsLocked(ITreeItem item)
        => LogRemoveRejectedItemIsLocked(this.logger, item.Label);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Remove rejected: orphan item: {ItemLabel}")]
    private static partial void LogRemoveRejectedOrphanItem(ILogger logger, string itemLabel);

    private void LogRemoveRejectedOrphanItem(ITreeItem item)
        => LogRemoveRejectedOrphanItem(this.logger, item.Label);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Remove rejected: Scene parent is not a ProjectAdapter: {ParentType}")]
    private static partial void LogRemoveRejectedSceneParentNotProject(ILogger logger, string parentType);

    private void LogRemoveRejectedSceneParentNotProject(object? parent)
        => LogRemoveRejectedSceneParentNotProject(this.logger, parent?.ToString() ?? "<null>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Remove rejected: Entity parent is invalid: {ParentType}")]
    private static partial void LogRemoveRejectedEntityParentInvalid(ILogger logger, string parentType);

    private void LogRemoveRejectedEntityParentInvalid(object? parent)
        => LogRemoveRejectedEntityParentInvalid(this.logger, parent?.ToString() ?? "<null>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Move rejected: {Reason}")]
    private static partial void LogMoveRejectedReason(ILogger logger, string? reason);

    private void LogMoveRejectedReason(string? reason)
        => LogMoveRejectedReason(this.logger, reason ?? "<null>");
}
