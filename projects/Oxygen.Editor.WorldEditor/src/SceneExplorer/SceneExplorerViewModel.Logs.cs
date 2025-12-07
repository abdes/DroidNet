// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.WorldEditor.SceneExplorer;

/// <summary>
/// Logging helpers for <see cref="SceneExplorerViewModel"/>.
/// This keeps LoggerMessage attributed methods in a separate partial type file,
/// following the repo convention (static partial + instance wrapper).
/// </summary>
public partial class SceneExplorerViewModel
{
    [LoggerMessage(Level = LogLevel.Debug, Message = "CreateFolderFromSelection invoked. SelectionModelType={Type}, ShownItemsCount={Count}")]
    private static partial void LogCreateFolderInvoked(ILogger logger, string? type, int count);

    private void LogCreateFolderInvoked(string? type, int count)
        => LogCreateFolderInvoked(this.logger, type, count);

    [LoggerMessage(Level = LogLevel.Debug, Message = "CreateFolderFromSelection captured selected ids: [{Ids}]")]
    private static partial void LogCreateFolderCapturedSelection(ILogger logger, string ids);

    private void LogCreateFolderCapturedSelection(string ids)
        => LogCreateFolderCapturedSelection(this.logger, ids);

    [LoggerMessage(Level = LogLevel.Debug, Message = "CreateFolderFromSelection: layout entries (top-level={TopCount}, total={TotalCount})")]
    private static partial void LogCreateFolderLayoutInfo(ILogger logger, int topCount, int totalCount);

    private void LogCreateFolderLayoutInfo(int topCount, int totalCount)
        => LogCreateFolderLayoutInfo(this.logger, topCount, totalCount);

    [LoggerMessage(Level = LogLevel.Debug, Message = "CreateFolderFromSelection: Removed {Count} entries into folder")]
    private static partial void LogCreateFolderRemovedEntriesCount(ILogger logger, int count);

    private void LogCreateFolderRemovedEntriesCount(int count)
        => LogCreateFolderRemovedEntriesCount(this.logger, count);

    [LoggerMessage(Level = LogLevel.Debug, Message = "CreateFolderFromSelection: SelectionModel yielded no items, falling back to ShownItems (found {Count} selected)")]
    private static partial void LogCreateFolderUsingShownItems(ILogger logger, int count);

    private void LogCreateFolderUsingShownItems(int count)
        => LogCreateFolderUsingShownItems(this.logger, count);

    [LoggerMessage(Level = LogLevel.Debug, Message = "CreateFolderFromSelection aborted: no selectable nodes found")]
    private static partial void LogCreateFolderNoSelection(ILogger logger);

    private void LogCreateFolderNoSelection()
        => LogCreateFolderNoSelection(this.logger);

    [LoggerMessage(Level = LogLevel.Debug, Message = "CreateFolderFromSelection: node adapter for {NodeId} not found when moving into folder")]
    private static partial void LogCreateFolderNodeAdapterNotFound(ILogger logger, System.Guid nodeId);

    private void LogCreateFolderNodeAdapterNotFound(System.Guid nodeId)
        => LogCreateFolderNodeAdapterNotFound(this.logger, nodeId);

    [LoggerMessage(Level = LogLevel.Debug, Message = "CreateFolderFromSelection: moved {Count} adapters into folder")]
    private static partial void LogCreateFolderMovedAdaptersCount(ILogger logger, int count);

    private void LogCreateFolderMovedAdaptersCount(int count)
        => LogCreateFolderMovedAdaptersCount(this.logger, count);

    [LoggerMessage(Level = LogLevel.Information, Message = "Created folder '{FolderName}' with {ChildCount} entries in scene {SceneId}")]
    private static partial void LogCreateFolderCreated(ILogger logger, string folderName, int childCount, System.Guid sceneId);

    private void LogCreateFolderCreated(string folderName, int childCount, System.Guid sceneId)
        => LogCreateFolderCreated(this.logger, folderName, childCount, sceneId);
}
