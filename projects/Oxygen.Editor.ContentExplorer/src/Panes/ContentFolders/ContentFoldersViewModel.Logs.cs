// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <inheritdoc cref="ContentFoldersViewModel"/>
public partial class ContentFoldersViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Could not refresh the published output folders.")]
    private static partial void LogCookedProjectionFailure(ILogger logger, Exception exception);

    private void LogCookedProjectionFailure(Exception exception) => LogCookedProjectionFailure(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "The project manager service does not have a currently loaded project.")]
    private static partial void LogNoCurrentProject(ILogger logger);

    private void LogNoCurrentProject()
        => LogNoCurrentProject(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to preload project folders during ViewModel activation.")]
    private static partial void LogPreloadingProjectFoldersError(ILogger logger, Exception ex);

    private void LogPreloadingProjectFoldersError(Exception ex)
        => LogPreloadingProjectFoldersError(this.logger, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "OnNavigatedToAsync: suppressTreeSelectionEvents = {Value}")]
    private static partial void LogSuppressTreeSelectionEvents(ILogger logger, bool value);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogSuppressTreeSelectionEvents(bool value)
        => LogSuppressTreeSelectionEvents(this.logger, value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "RestoreState: Restoring from URL query parameters")]
    private static partial void LogRestoreStateStart(ILogger logger);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogRestoreStateStart()
        => LogRestoreStateStart(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "RestoreState: Adding folder from URL: {Path}")]
    private static partial void LogRestoreStateAddFolder(ILogger logger, AssetPath path);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogRestoreStateAddFolder(AssetPath path)
        => LogRestoreStateAddFolder(this.logger, path);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "RestoreState: Final ContentExplorerState.SelectedFolders: [{Folders}]")]
    private static partial void LogRestoreStateFinal(ILogger logger, string folders);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogRestoreStateFinal(IEnumerable<AssetPath> folders)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var joined = string.Join(", ", folders);
        LogRestoreStateFinal(this.logger, joined);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "OnTreeSelectionChanged called: PropertyName={PropertyName}, isUpdatingFromState={IsUpdating}, suppressTreeSelectionEvents={Suppress}")]
    private static partial void LogTreeSelectionChanged(ILogger logger, string? propertyName, bool isUpdating, bool suppress);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogTreeSelectionChanged(string? propertyName, bool isUpdating, bool suppress)
        => LogTreeSelectionChanged(this.logger, propertyName, isUpdating, suppress);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "OnTreeSelectionChanged - early return. isUpdatingFromState={IsUpdating}, suppressTreeSelectionEvents={Suppress}, PropertyName={PropertyName}, SelectionModel type={Type}")]
    private static partial void LogTreeSelectionChangedEarlyReturn(ILogger logger, bool isUpdating, bool suppress, string? propertyName, string? type);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogTreeSelectionChangedEarlyReturn(bool isUpdating, bool suppress, string? propertyName, object? selectionModel)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var type = selectionModel?.GetType().Name;
        LogTreeSelectionChangedEarlyReturn(this.logger, isUpdating, suppress, propertyName, type);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tree selection changed, updating ContentExplorerState. SelectedIndices count: {Count}")]
    private static partial void LogTreeSelectionChangedUpdatingState(ILogger logger, int count);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogTreeSelectionChangedUpdatingState(int count)
        => LogTreeSelectionChangedUpdatingState(this.logger, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Selected folders: [{Folders}]")]
    private static partial void LogSelectedFolders(ILogger logger, string folders);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogSelectedFolders(IEnumerable<AssetPath> folders)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var joined = string.Join(", ", folders);
        LogSelectedFolders(this.logger, joined);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Updating ContentExplorerState with {Count} selected folders")]
    private static partial void LogUpdatingContentExplorerState(ILogger logger, int count);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogUpdatingContentExplorerState(int count)
        => LogUpdatingContentExplorerState(this.logger, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ContentExplorerState updated successfully")]
    private static partial void LogContentExplorerStateUpdated(ILogger logger);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogContentExplorerStateUpdated()
        => LogContentExplorerStateUpdated(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "OnContentExplorerStatePropertyChanged called: PropertyName={PropertyName}")]
    private static partial void LogContentExplorerStatePropertyChanged(ILogger logger, string? propertyName);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogContentExplorerStatePropertyChanged(string? propertyName)
        => LogContentExplorerStatePropertyChanged(this.logger, propertyName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ContentExplorerState.SelectedFolders changed. New selection: [{Folders}]")]
    private static partial void LogContentExplorerStateSelectedFoldersChanged(ILogger logger, string folders);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogContentExplorerStateSelectedFoldersChanged(IEnumerable<AssetPath> folders)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var joined = string.Join(", ", folders);
        LogContentExplorerStateSelectedFoldersChanged(this.logger, joined);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Folder rename requested in the sources tree: '{Before}' -> '{After}'.")]
    private static partial void LogFolderRenameRequested(ILogger logger, AssetPath before, AssetPath after);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogFolderRenameRequested(AssetPath before, AssetPath after)
        => LogFolderRenameRequested(this.logger, before, after);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Folder drop in the sources tree: {Operation} [{Folders}] into '{Parent}'.")]
    private static partial void LogFolderDropRequested(ILogger logger, DroidNet.Controls.TreeDropOperation operation, string folders, AssetPath parent);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogFolderDropRequested(DroidNet.Controls.TreeDropOperation operation, IEnumerable<AssetPath> folders, AssetPath parent)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var joined = string.Join(", ", folders);
        LogFolderDropRequested(this.logger, operation, joined, parent);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Folder '{Folder}' no longer exists; showing its nearest existing folder '{Shown}' instead, or the whole project when none is.")]
    private static partial void LogMissingSelectedFolder(ILogger logger, AssetPath folder, AssetPath? shown);

    private void LogMissingSelectedFolder(AssetPath folder, AssetPath? shown)
        => LogMissingSelectedFolder(this.logger, folder, shown);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Reloading the sources tree: the asset catalog reported changed folders.")]
    private static partial void LogReloadingTreeAfterFolderChanges(ILogger logger);

    private void LogReloadingTreeAfterFolderChanges()
        => LogReloadingTreeAfterFolderChanges(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Folder delete in the sources tree: [{Folders}].")]
    private static partial void LogFolderDeleteRequested(ILogger logger, string folders);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogFolderDeleteRequested(IEnumerable<AssetPath> folders)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var joined = string.Join(", ", folders);
        LogFolderDeleteRequested(this.logger, joined);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Reloading the sources tree after {Moves} folder moves and {Deleted} deletes.")]
    private static partial void LogReloadingTreeAfterFileChanges(ILogger logger, int moves, int deleted);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogReloadingTreeAfterFileChanges(int moves, int deleted)
        => LogReloadingTreeAfterFileChanges(this.logger, moves, deleted);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Content mount update failed.")]
    private static partial void LogMountChangeFailed(ILogger logger, Exception exception);

    private void LogMountChangeFailed(Exception exception) => LogMountChangeFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Pak mount '{Name}' ('{Path}') could not be read; the Content Explorer does not show it.")]
    private static partial void LogPakMountFailure(ILogger logger, Exception exception, string name, FileSystemPath path);

    private void LogPakMountFailure(Exception exception, string name, FileSystemPath path)
        => LogPakMountFailure(this.logger, exception, name, path);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to restore folder mount {Name}.")]
    private static partial void LogMountRestoreFailure(ILogger logger, Exception exception, string name);

    private void LogMountRestoreFailure(Exception exception, string name)
        => LogMountRestoreFailure(this.logger, exception, name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to navigate to folder.")]
    private static partial void LogFolderNavigationFailure(ILogger logger, Exception exception);

    private void LogFolderNavigationFailure(Exception exception) => LogFolderNavigationFailure(this.logger, exception);
}
