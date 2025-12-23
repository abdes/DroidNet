// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <inheritdoc cref="ProjectLayoutViewModel"/>
public partial class ProjectLayoutViewModel
{
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
        Level = LogLevel.Debug,
        Message = "OnNavigatedToAsync: suppressTreeSelectionEvents = {Value}")]
    private static partial void LogSuppressTreeSelectionEvents(ILogger logger, bool value);

    private void LogSuppressTreeSelectionEvents(bool value)
        => LogSuppressTreeSelectionEvents(this.logger, value);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "RestoreState: Restoring from URL query parameters")]
    private static partial void LogRestoreStateStart(ILogger logger);

    private void LogRestoreStateStart()
        => LogRestoreStateStart(this.logger);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "RestoreState: Adding folder from URL: {Path}")]
    private static partial void LogRestoreStateAddFolder(ILogger logger, string path);

    private void LogRestoreStateAddFolder(string path)
        => LogRestoreStateAddFolder(this.logger, path);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "RestoreState: Final ContentBrowserState.SelectedFolders: [{Folders}]")]
    private static partial void LogRestoreStateFinal(ILogger logger, string folders);

    private void LogRestoreStateFinal(string folders)
        => LogRestoreStateFinal(this.logger, folders);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "OnTreeSelectionChanged called: PropertyName={PropertyName}, isUpdatingFromState={IsUpdating}, suppressTreeSelectionEvents={Suppress}")]
    private static partial void LogTreeSelectionChanged(ILogger logger, string? propertyName, bool isUpdating, bool suppress);

    private void LogTreeSelectionChanged(string? propertyName, bool isUpdating, bool suppress)
        => LogTreeSelectionChanged(this.logger, propertyName, isUpdating, suppress);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "OnTreeSelectionChanged - early return. isUpdatingFromState={IsUpdating}, suppressTreeSelectionEvents={Suppress}, PropertyName={PropertyName}, SelectionModel type={Type}")]
    private static partial void LogTreeSelectionChangedEarlyReturn(ILogger logger, bool isUpdating, bool suppress, string? propertyName, string? type);

    private void LogTreeSelectionChangedEarlyReturn(bool isUpdating, bool suppress, string? propertyName, string? type)
        => LogTreeSelectionChangedEarlyReturn(this.logger, isUpdating, suppress, propertyName, type);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Tree selection changed, updating ContentBrowserState. SelectedIndices count: {Count}")]
    private static partial void LogTreeSelectionChangedUpdatingState(ILogger logger, int count);

    private void LogTreeSelectionChangedUpdatingState(int count)
        => LogTreeSelectionChangedUpdatingState(this.logger, count);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Selected folders: [{Folders}]")]
    private static partial void LogSelectedFolders(ILogger logger, string folders);

    private void LogSelectedFolders(string folders)
        => LogSelectedFolders(this.logger, folders);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Updating ContentBrowserState with {Count} selected folders")]
    private static partial void LogUpdatingContentBrowserState(ILogger logger, int count);

    private void LogUpdatingContentBrowserState(int count)
        => LogUpdatingContentBrowserState(this.logger, count);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "ContentBrowserState updated successfully")]
    private static partial void LogContentBrowserStateUpdated(ILogger logger);

    private void LogContentBrowserStateUpdated()
        => LogContentBrowserStateUpdated(this.logger);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "OnContentBrowserStatePropertyChanged called: PropertyName={PropertyName}")]
    private static partial void LogContentBrowserStatePropertyChanged(ILogger logger, string? propertyName);

    private void LogContentBrowserStatePropertyChanged(string? propertyName)
        => LogContentBrowserStatePropertyChanged(this.logger, propertyName);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "ContentBrowserState.SelectedFolders changed. New selection: [{Folders}]")]
    private static partial void LogContentBrowserStateSelectedFoldersChanged(ILogger logger, string folders);

    private void LogContentBrowserStateSelectedFoldersChanged(string folders)
        => LogContentBrowserStateSelectedFoldersChanged(this.logger, folders);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "UpdateTreeSelectionFromStateAsync - projectRoot is null, returning")]
    private static partial void LogUpdateTreeSelectionProjectRootNull(ILogger logger);

    private void LogUpdateTreeSelectionProjectRootNull()
        => LogUpdateTreeSelectionProjectRootNull(this.logger);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Updating tree selection from ContentBrowserState")]
    private static partial void LogUpdateTreeSelectionStart(ILogger logger);

    private void LogUpdateTreeSelectionStart()
        => LogUpdateTreeSelectionStart(this.logger);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Set isUpdatingFromState = {Value}")]
    private static partial void LogSetIsUpdatingFromState(ILogger logger, bool value);

    private void LogSetIsUpdatingFromState(bool value)
        => LogSetIsUpdatingFromState(this.logger, value);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Selected paths to sync: [{Paths}]")]
    private static partial void LogSelectedPathsToSync(ILogger logger, string paths);

    private void LogSelectedPathsToSync(string paths)
        => LogSelectedPathsToSync(this.logger, paths);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Cleared all tree item selections")]
    private static partial void LogClearedTreeItemSelections(ILogger logger);

    private void LogClearedTreeItemSelections()
        => LogClearedTreeItemSelections(this.logger);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Skipping empty path")]
    private static partial void LogSkippingEmptyPath(ILogger logger);

    private void LogSkippingEmptyPath()
        => LogSkippingEmptyPath(this.logger);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Setting IsSelected=true for path: {Path}")]
    private static partial void LogSettingIsSelected(ILogger logger, string path);

    private void LogSettingIsSelected(string path)
        => LogSettingIsSelected(this.logger, path);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Could not find folder adapter for path: {Path}")]
    private static partial void LogFolderAdapterNotFound(ILogger logger, string path);

    private void LogFolderAdapterNotFound(string path)
        => LogFolderAdapterNotFound(this.logger, path);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Completed tree selection update from ContentBrowserState")]
    private static partial void LogUpdateTreeSelectionCompleted(ILogger logger);

    private void LogUpdateTreeSelectionCompleted()
        => LogUpdateTreeSelectionCompleted(this.logger);
}
