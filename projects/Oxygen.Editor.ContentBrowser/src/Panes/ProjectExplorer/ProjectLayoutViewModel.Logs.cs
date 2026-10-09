// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

#pragma warning disable SA1204 // Static elements should appear before instance elements

/// <inheritdoc cref="ProjectLayoutViewModel"/>
public partial class ProjectLayoutViewModel
{
    [LoggerMessage(Level = LogLevel.Error, Message = "Could not refresh the published output folders.")]
    private partial void LogCookedProjectionFailure(Exception exception);

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
    private static partial void LogRestoreStateAddFolder(ILogger logger, string path);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogRestoreStateAddFolder(string path)
        => LogRestoreStateAddFolder(this.logger, path);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "RestoreState: Final ContentBrowserState.SelectedFolders: [{Folders}]")]
    private static partial void LogRestoreStateFinal(ILogger logger, string folders);

    private void LogRestoreStateFinal(string folders)
        => LogRestoreStateFinal(this.logger, folders);

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
    private void LogTreeSelectionChangedEarlyReturn(bool isUpdating, bool suppress, string? propertyName, string? type)
        => LogTreeSelectionChangedEarlyReturn(this.logger, isUpdating, suppress, propertyName, type);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tree selection changed, updating ContentBrowserState. SelectedIndices count: {Count}")]
    private static partial void LogTreeSelectionChangedUpdatingState(ILogger logger, int count);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogTreeSelectionChangedUpdatingState(int count)
        => LogTreeSelectionChangedUpdatingState(this.logger, count);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Selected folders: [{Folders}]")]
    private static partial void LogSelectedFolders(ILogger logger, string folders);

    private void LogSelectedFolders(string folders)
        => LogSelectedFolders(this.logger, folders);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Updating ContentBrowserState with {Count} selected folders")]
    private static partial void LogUpdatingContentBrowserState(ILogger logger, int count);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogUpdatingContentBrowserState(int count)
        => LogUpdatingContentBrowserState(this.logger, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ContentBrowserState updated successfully")]
    private static partial void LogContentBrowserStateUpdated(ILogger logger);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogContentBrowserStateUpdated()
        => LogContentBrowserStateUpdated(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "OnContentBrowserStatePropertyChanged called: PropertyName={PropertyName}")]
    private static partial void LogContentBrowserStatePropertyChanged(ILogger logger, string? propertyName);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogContentBrowserStatePropertyChanged(string? propertyName)
        => LogContentBrowserStatePropertyChanged(this.logger, propertyName);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "ContentBrowserState.SelectedFolders changed. New selection: [{Folders}]")]
    private static partial void LogContentBrowserStateSelectedFoldersChanged(ILogger logger, string folders);

    private void LogContentBrowserStateSelectedFoldersChanged(string folders)
        => LogContentBrowserStateSelectedFoldersChanged(this.logger, folders);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "UpdateTreeSelectionFromStateAsync - projectRoot is null, returning")]
    private static partial void LogUpdateTreeSelectionProjectRootNull(ILogger logger);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogUpdateTreeSelectionProjectRootNull()
        => LogUpdateTreeSelectionProjectRootNull(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Updating tree selection from ContentBrowserState")]
    private static partial void LogUpdateTreeSelectionStart(ILogger logger);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogUpdateTreeSelectionStart()
        => LogUpdateTreeSelectionStart(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Set isUpdatingFromState = {Value}")]
    private static partial void LogSetIsUpdatingFromState(ILogger logger, bool value);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogSetIsUpdatingFromState(bool value)
        => LogSetIsUpdatingFromState(this.logger, value);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Selected paths to sync: [{Paths}]")]
    private static partial void LogSelectedPathsToSync(ILogger logger, string paths);

    private void LogSelectedPathsToSync(string paths)
        => LogSelectedPathsToSync(this.logger, paths);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Cleared all tree item selections")]
    private static partial void LogClearedTreeItemSelections(ILogger logger);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogClearedTreeItemSelections()
        => LogClearedTreeItemSelections(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Skipping empty path")]
    private static partial void LogSkippingEmptyPath(ILogger logger);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogSkippingEmptyPath()
        => LogSkippingEmptyPath(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Setting IsSelected=true for path: {Path}")]
    private static partial void LogSettingIsSelected(ILogger logger, string path);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogSettingIsSelected(string path)
        => LogSettingIsSelected(this.logger, path);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Could not find folder adapter for path: {Path}")]
    private static partial void LogFolderAdapterNotFound(ILogger logger, string path);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogFolderAdapterNotFound(string path)
        => LogFolderAdapterNotFound(this.logger, path);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Completed tree selection update from ContentBrowserState")]
    private static partial void LogUpdateTreeSelectionCompleted(ILogger logger);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogUpdateTreeSelectionCompleted()
        => LogUpdateTreeSelectionCompleted(this.logger);

    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "Folder rename requested in the sources tree: '{Before}' -> '{After}'.")]
    private static partial void LogFolderRenameRequested(ILogger logger, string before, string after);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogFolderRenameRequested(string before, string after)
        => LogFolderRenameRequested(this.logger, before, after);

    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "Folder drop in the sources tree: {Operation} [{Folders}] into '{Parent}'.")]
    private static partial void LogFolderDropRequested(ILogger logger, DroidNet.Controls.TreeDropOperation operation, string folders, string parent);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogFolderDropRequested(DroidNet.Controls.TreeDropOperation operation, IEnumerable<string> folders, string parent)
#pragma warning disable CA1873 // Compiled out of release builds by Conditional("DEBUG")
        => LogFolderDropRequested(this.logger, operation, string.Join(", ", folders), parent);
#pragma warning restore CA1873

    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "Reloading the sources tree after {Moves} folder moves and {Deleted} deletes.")]
    private static partial void LogReloadingTreeAfterFileChanges(ILogger logger, int moves, int deleted);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogReloadingTreeAfterFileChanges(int moves, int deleted)
        => LogReloadingTreeAfterFileChanges(this.logger, moves, deleted);
}
