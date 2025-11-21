// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls;
using DroidNet.Controls.Selection;
using DroidNet.Routing;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Storage;
using Oxygen.Editor.WorldEditor.ContentBrowser;

namespace Oxygen.Editor.WorldEditor.ProjectExplorer;

/// <summary>
///     Represents the ViewModel for the project layout in the content browser.
///     Acts as a mediator between the DynamicTree control and ContentBrowserState.
/// </summary>
/// <param name="projectManager">The project manager service.</param>
/// <param name="contentBrowserState">The state of the content browser.</param>
/// <param name="loggerFactory">
///     Optional factory for creating loggers. If provided, enables detailed logging of the recognition
///     process. If <see langword="null" />, logging is disabled.
/// </param>
public partial class ProjectLayoutViewModel(
    IProjectManagerService projectManager,
    ContentBrowserState contentBrowserState,
    ILoggerFactory? loggerFactory)
    : DynamicTreeViewModel, IRoutingAware, IDisposable
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<ProjectLayoutViewModel>() ??
                                      NullLoggerFactory.Instance.CreateLogger<ProjectLayoutViewModel>();

    private IActiveRoute? activeRoute;
    private bool isUpdatingFromState;
    private FolderTreeItemAdapter? projectRoot;
    private bool suppressTreeSelectionEvents;

    /// <summary>
    ///     Disposes the resources used by the ProjectLayoutViewModel.
    /// </summary>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <inheritdoc />
    public async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        this.activeRoute = route;

        // Subscribe to ContentBrowserState changes
        contentBrowserState.PropertyChanged += this.OnContentBrowserStatePropertyChanged;

        // Suppress tree selection changes while restoring from route and initializing tree
        this.suppressTreeSelectionEvents = true;
        this.LogSuppressTreeSelectionEvents(value: true);

        try
        {
            // 1) Restore state from the URL
            this.RestoreState();

            // 2) Ensure the tree is initialized (loads/expands nodes)
            await this.PreloadRecentTemplatesAsync().ConfigureAwait(true);

            // 3) Apply restored state to the tree selection now that items exist
            await this.UpdateTreeSelectionFromStateAsync().ConfigureAwait(true);
        }
        finally
        {
            this.suppressTreeSelectionEvents = false;
            this.LogSuppressTreeSelectionEvents(value: false);
        }
    }

    /// <summary>
    ///     Refreshes the project explorer tree by recreating the root node and reinitializing the tree view.
    ///     Preserves the current selection from ContentBrowserState.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous refresh operation.</returns>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "pre-loading happens during route activation and we cannot report exceptions in that stage")]
    public async Task RefreshTreeAsync()
    {
        // Rebuild the project root to force a fresh load of children
        try
        {
            // Preserve the current selection BEFORE we touch the tree (InitializeRootAsync clears selection)
            var previousSelection = contentBrowserState.SelectedFolders.ToList();

            // Temporarily suppress tree selection updates causing state churn while we rebuild
            this.suppressTreeSelectionEvents = true;

            var projectInfo = this.GetCurrentProjectInfo() ??
                              throw new InvalidOperationException("Project Layout used with no CurrentProject");

            var storage = projectManager.GetCurrentProjectStorageProvider();
            var folder = await storage.GetFolderFromPathAsync(projectInfo.Location!).ConfigureAwait(true);

            // Dispose previous root to release resources
            this.projectRoot?.Dispose();

            this.projectRoot =
                new FolderTreeItemAdapter(this.logger, folder, projectInfo.Name, isRoot: true) { IsExpanded = true };

            // Reinitialize the tree UI
            await this.InitializeRootAsync(this.projectRoot, skipRoot: false).ConfigureAwait(true);

            // Allow selection changes to flow again for the explicit selection we apply next
            this.suppressTreeSelectionEvents = false;

            // Reapply previous selection if it still exists, prioritizing non-root paths
            var candidates = previousSelection
                .Where(p => !string.IsNullOrEmpty(p) && !string.Equals(p, ".", StringComparison.Ordinal))
                .Order(StringComparer.Ordinal)
                .ToList();

            FolderTreeItemAdapter? target = null;
            if (candidates.Count > 0 && this.projectRoot is not null)
            {
                foreach (var path in candidates)
                {
                    var adapter = await this.FindFolderAdapterAsync(this.projectRoot, path).ConfigureAwait(true);
                    if (adapter is not null)
                    {
                        target = adapter;
                        break;
                    }
                }
            }

            // Fallback to root if none of the candidates were found, or if previous selection explicitly was root
            target ??= this.projectRoot;

            if (target is not null)
            {
                this.ClearAndSelectItem(target);
            }
        }
        catch (Exception ex)
        {
            this.LogPreloadingProjectFoldersError(ex);
        }
    }

    /// <summary>
    ///     Navigates to and selects the specified folder in the tree.
    ///     This is the entry point for programmatic navigation from other ViewModels.
    /// </summary>
    /// <param name="folder">The folder to navigate to and select.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous refresh operation.</returns>
    public async Task NavigateToFolderAsync(IFolder folder)
    {
        if (this.projectRoot == null)
        {
            return;
        }

        var pathRelativeToProjectRoot = folder.GetPathRelativeTo(contentBrowserState.ProjectRootPath);
        var folderAdapter = await this.FindFolderAdapterAsync(this.projectRoot, pathRelativeToProjectRoot).ConfigureAwait(true);
        if (folderAdapter != null)
        {
            // Use the tree control's selection API to properly select the folder
            // The tree control will handle updating ContentBrowserState through FolderTreeItemAdapter
            this.ClearAndSelectItem(folderAdapter);
        }
    }

    /// <summary>
    ///     Protected dispose pattern implementation.
    /// </summary>
    /// <param name="disposing">True if called from Dispose; false if called from finalizer.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (disposing)
        {
            contentBrowserState.PropertyChanged -= this.OnContentBrowserStatePropertyChanged;
            this.projectRoot?.Dispose();
        }

        // No unmanaged resources to clean up.
    }

    /// <inheritdoc />
    protected override void OnSelectionModelChanged(SelectionModel<ITreeItem>? oldValue)
    {
        base.OnSelectionModelChanged(oldValue);

        // Unsubscribe from old selection model
        if (oldValue is not null)
        {
            oldValue.PropertyChanged -= this.OnTreeSelectionChanged;
        }

        // Subscribe to new selection model
        if (this.SelectionModel is not null)
        {
            this.SelectionModel.PropertyChanged += this.OnTreeSelectionChanged;
        }
    }

    /// <summary>
    ///     Recursively sets the IsSelected property on tree items.
    /// </summary>
    private static async Task SetTreeItemSelectionRecursively(FolderTreeItemAdapter adapter, bool isSelected)
    {
        adapter.IsSelected = isSelected;

        var children = await adapter.Children.ConfigureAwait(true);
        foreach (var child in children.OfType<FolderTreeItemAdapter>())
        {
            await SetTreeItemSelectionRecursively(child, isSelected).ConfigureAwait(true);
        }
    }

    private void RestoreState()
    {
        Debug.Assert(this.activeRoute is not null, "should have an active route");

        this.LogRestoreStateStart();

        // CLEAR existing selection first - this is the key fix!
        contentBrowserState.SelectedFolders.Clear();

        var selectedFolders = this.activeRoute.QueryParams.GetValues("selected");
        if (selectedFolders is not null)
        {
            foreach (var relativePath in selectedFolders)
            {
                if (!string.IsNullOrEmpty(relativePath))
                {
                    this.LogRestoreStateAddFolder(relativePath);
                    _ = contentBrowserState.SelectedFolders.Add(relativePath);
                }
            }
        }

        this.LogRestoreStateFinal(string.Join(", ", contentBrowserState.SelectedFolders));
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "pre-loading happens during route activation and we cannot report exceptions in that stage")]
    private async Task PreloadRecentTemplatesAsync()
    {
        try
        {
            if (this.projectRoot is null)
            {
                // The following method will do sanity checks on the current project and its info. On successful return, we have
                // guarantee the project info is valid and has a valid location for the project root folder.
                var projectInfo = this.GetCurrentProjectInfo() ??
                                  throw new InvalidOperationException("Project Layout used with no CurrentProject");

                // Create the root TreeItem for the project root folder.
                var storage = projectManager.GetCurrentProjectStorageProvider();
                var folder = await storage.GetFolderFromPathAsync(projectInfo.Location!).ConfigureAwait(true);
                this.projectRoot =
                    new FolderTreeItemAdapter(this.logger, folder, projectInfo.Name, isRoot: true) { IsExpanded = true };
            }

            // Preload the project folders
            await this.LoadProjectAsync().ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            this.LogPreloadingProjectFoldersError(ex);
        }
    }

    /// <summary>
    ///     Loads the project asynchronously, starting with the project root folder, and continuing with children that are part
    ///     of the
    ///     initial selection set. The selection set can be provided via the navigation URL as query parameters.
    /// </summary>
    /// <returns>
    ///     A <see cref="Task" /> object representing the asynchronous work.
    /// </returns>
    [RelayCommand]
    private async Task LoadProjectAsync()
    {
        Debug.Assert(this.projectRoot is not null, "project root node should be initialized");
        Debug.Assert(this.activeRoute is not null, "should have an active route");

        // We will expand the entire project tree
        await this.InitializeRootAsync(this.projectRoot, skipRoot: false).ConfigureAwait(true);
    }

    private IProjectInfo? GetCurrentProjectInfo()
    {
        var projectInfo = projectManager.CurrentProject?.ProjectInfo;

        if (projectInfo is null)
        {
            this.LogNoCurrentProject();
        }
#if DEBUG
        else
        {
            Debug.Assert(
                projectInfo.Location is not null,
                "current project must be set, have a valid ProjectInfo and a valid Location");
        }
#endif

        return projectInfo;
    }

    /// <summary>
    ///     Recursively finds a folder adapter by its relative path.
    /// </summary>
    /// <param name="currentAdapter">The current adapter to search from.</param>
    /// <param name="targetPath">The target relative path to find.</param>
    /// <returns>The folder adapter if found, null otherwise.</returns>
    private async Task<FolderTreeItemAdapter?> FindFolderAdapterAsync(
        FolderTreeItemAdapter currentAdapter,
        string targetPath)
    {
        // Get the relative path of the current adapter
        var currentPath = currentAdapter.Folder.GetPathRelativeTo(contentBrowserState.ProjectRootPath);

        // Normalize paths for comparison (handle . for root)
        if (string.Equals(currentPath, ".", StringComparison.Ordinal))
        {
            currentPath = string.Empty;
        }

        if (string.Equals(targetPath, ".", StringComparison.Ordinal))
        {
            targetPath = string.Empty;
        }

        if (string.Equals(currentPath, targetPath, StringComparison.OrdinalIgnoreCase))
        {
            return currentAdapter;
        }

        // Search in children
        var children = await currentAdapter.Children.ConfigureAwait(true);
        foreach (var child in children.OfType<FolderTreeItemAdapter>())
        {
            var result = await this.FindFolderAdapterAsync(child, targetPath).ConfigureAwait(true);
            if (result != null)
            {
                return result;
            }
        }

        return null;
    }

    /// <summary>
    ///     Handles tree selection changes and updates ContentBrowserState accordingly.
    ///     This prevents duplicate history entries by using atomic operations.
    /// </summary>
    private void OnTreeSelectionChanged(object? sender, PropertyChangedEventArgs e)
    {
        this.LogTreeSelectionChanged(e.PropertyName, this.isUpdatingFromState, this.suppressTreeSelectionEvents);

        // Only handle SelectedIndex changes for MultipleSelectionModel to avoid infinite loops
        // SelectedIndex changes when the selection changes, so we only need to listen to this one property
        if (this.isUpdatingFromState
            || this.suppressTreeSelectionEvents
            || !string.Equals(e.PropertyName, nameof(MultipleSelectionModel<>.SelectedIndex), StringComparison.Ordinal)
            || this.SelectionModel is not MultipleSelectionModel<ITreeItem> multipleSelection)
        {
            this.LogTreeSelectionChangedEarlyReturn(
                this.isUpdatingFromState,
                this.suppressTreeSelectionEvents,
                e.PropertyName,
                this.SelectionModel?.GetType().Name);
            return;
        }

        this.LogTreeSelectionChangedUpdatingState(multipleSelection.SelectedIndices.Count);

        // Get currently selected folder adapters from ALL selected indices
        var selectedFolders = multipleSelection.SelectedIndices
            .Select(index => this.ShownItems[index])
            .OfType<FolderTreeItemAdapter>()
            .Select(adapter => adapter.Folder.GetPathRelativeTo(contentBrowserState.ProjectRootPath))
            .ToList();

        this.LogSelectedFolders(string.Join(", ", selectedFolders));

        // Update ContentBrowserState
        this.LogUpdatingContentBrowserState(selectedFolders.Count);
        contentBrowserState.SetSelectedFolders(selectedFolders);

        this.LogContentBrowserStateUpdated();
    }

    /// <summary>
    ///     Handles ContentBrowserState changes and updates tree selection accordingly.
    /// </summary>
    private async void OnContentBrowserStatePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        this.LogContentBrowserStatePropertyChanged(e.PropertyName);

        if (string.Equals(e.PropertyName, nameof(ContentBrowserState.SelectedFolders), StringComparison.Ordinal))
        {
            this.LogContentBrowserStateSelectedFoldersChanged(string.Join(", ", contentBrowserState.SelectedFolders));
            await this.UpdateTreeSelectionFromStateAsync().ConfigureAwait(true);
        }
    }

    /// <summary>
    ///     Updates tree selection to match ContentBrowserState.
    ///     Uses atomic operations to prevent feedback loops and duplicate history entries.
    /// </summary>
    private async Task UpdateTreeSelectionFromStateAsync()
    {
        if (this.projectRoot == null)
        {
            this.LogUpdateTreeSelectionProjectRootNull();
            return;
        }

        this.LogUpdateTreeSelectionStart();

        try
        {
            this.isUpdatingFromState = true;
            this.LogSetIsUpdatingFromState(value: true);

            var selectedPaths = contentBrowserState.SelectedFolders.ToList();
            this.LogSelectedPathsToSync(string.Join(", ", selectedPaths));

            // First, clear all current selections in the tree items
            await this.ClearAllTreeItemSelections().ConfigureAwait(true);
            this.LogClearedTreeItemSelections();

            // Then set IsSelected=true for the items that should be selected
            foreach (var path in selectedPaths)
            {
                // Skip empty paths - they cause issues
                if (string.IsNullOrEmpty(path))
                {
                    this.LogSkippingEmptyPath();
                    continue;
                }

                var folderAdapter = await this.FindFolderAdapterAsync(this.projectRoot, path).ConfigureAwait(true);
                if (folderAdapter != null)
                {
                    this.LogSettingIsSelected(path);
                    folderAdapter.IsSelected = true;
                }
                else
                {
                    this.LogFolderAdapterNotFound(path);
                }
            }

            this.LogUpdateTreeSelectionCompleted();
        }
        finally
        {
            this.isUpdatingFromState = false;
            this.LogSetIsUpdatingFromState(value: false);
        }
    }

    /// <summary>
    ///     Clears the IsSelected property on all tree items.
    /// </summary>
    private async Task ClearAllTreeItemSelections()
    {
        if (this.projectRoot == null)
        {
            return;
        }

        await SetTreeItemSelectionRecursively(this.projectRoot, isSelected: false).ConfigureAwait(true);
    }
}
