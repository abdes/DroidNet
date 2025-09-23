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

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
///     Represents the ViewModel for the project layout in the content browser.
///     Acts as a mediator between the DynamicTree control and ContentBrowserState.
/// </summary>
/// <param name="projectManager">The project manager service.</param>
/// <param name="contentBrowserState">The state of the content browser.</param>
/// <param name="router">The router service for navigation.</param>
/// <param name="loggerFactory">The logger factory to create loggers.</param>
public partial class ProjectLayoutViewModel(
    IProjectManagerService projectManager,
    ContentBrowserState contentBrowserState,
    IRouter router,
    ILoggerFactory? loggerFactory)
    : DynamicTreeViewModel, IRoutingAware, IDisposable
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<ProjectLayoutViewModel>() ??
                                      NullLoggerFactory.Instance.CreateLogger<ProjectLayoutViewModel>();

    private IActiveRoute? activeRoute;
    private FolderTreeItemAdapter? projectRoot;
    private bool isUpdatingFromState;
    private bool suppressTreeSelectionEvents;

    /// <summary>
    ///     Disposes the resources used by the ProjectLayoutViewModel.
    /// </summary>
    public void Dispose()
    {
        contentBrowserState.PropertyChanged -= this.OnContentBrowserStatePropertyChanged;
        this.projectRoot?.Dispose();
    }

    /// <inheritdoc />
    public async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        this.activeRoute = route;

        // Subscribe to ContentBrowserState changes
        contentBrowserState.PropertyChanged += this.OnContentBrowserStatePropertyChanged;

        // Suppress tree selection changes while restoring from route and initializing tree
        this.suppressTreeSelectionEvents = true;
        Debug.WriteLine("[ProjectLayoutViewModel] OnNavigatedToAsync: suppressTreeSelectionEvents = true");

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
            Debug.WriteLine("[ProjectLayoutViewModel] OnNavigatedToAsync: suppressTreeSelectionEvents = false");
        }
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

    private void RestoreState()
    {
        Debug.Assert(this.activeRoute is not null, "should have an active route");

        Debug.WriteLine($"[ProjectLayoutViewModel] RestoreState: Restoring from URL query parameters");

        // CLEAR existing selection first - this is the key fix!
        contentBrowserState.SelectedFolders.Clear();

        var selectedFolders = this.activeRoute.QueryParams.GetValues("selected");
        if (selectedFolders is not null)
        {
            foreach (var relativePath in selectedFolders)
            {
                if (!string.IsNullOrEmpty(relativePath))
                {
                    Debug.WriteLine($"[ProjectLayoutViewModel] RestoreState: Adding folder from URL: {relativePath}");
                    _ = contentBrowserState.SelectedFolders.Add(relativePath);
                }
            }
        }

        Debug.WriteLine($"[ProjectLayoutViewModel] RestoreState: Final ContentBrowserState.SelectedFolders: [{string.Join(", ", contentBrowserState.SelectedFolders)}]");
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types",
        Justification = "pre-loading happens during route activation and we cannot report exceptions in that stage")]
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
                    new FolderTreeItemAdapter(this.logger, folder, projectInfo.Name, true)
                    {
                        IsExpanded = true,
                    };
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
        await this.InitializeRootAsync(this.projectRoot, false).ConfigureAwait(true);
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
    ///     Navigates to and selects the specified folder in the tree.
    ///     This is the entry point for programmatic navigation from other ViewModels.
    /// </summary>
    /// <param name="folder">The folder to navigate to and select.</param>
    public async Task NavigateToFolderAsync(IFolder folder)
    {
        if (this.projectRoot == null)
        {
            return;
        }

        var pathRelativeToProjectRoot = folder.GetPathRelativeTo(contentBrowserState.ProjectRootPath);
        var folderAdapter = await this.FindFolderAdapterAsync(this.projectRoot, pathRelativeToProjectRoot);
        if (folderAdapter != null)
        {
            // Use the tree control's selection API to properly select the folder
            this.ClearAndSelectItem(folderAdapter);
            // The tree control will handle updating ContentBrowserState through FolderTreeItemAdapter
        }
    }

    /// <summary>
    /// Recursively finds a folder adapter by its relative path.
    /// </summary>
    /// <param name="currentAdapter">The current adapter to search from.</param>
    /// <param name="targetPath">The target relative path to find.</param>
    /// <returns>The folder adapter if found, null otherwise.</returns>
    private async Task<FolderTreeItemAdapter?> FindFolderAdapterAsync(FolderTreeItemAdapter currentAdapter, string targetPath)
    {
        // Get the relative path of the current adapter
        var currentPath = currentAdapter.Folder.GetPathRelativeTo(contentBrowserState.ProjectRootPath);

        // Normalize paths for comparison (handle . for root)
        if (currentPath == ".")
        {
            currentPath = "";
        }
        if (targetPath == ".")
        {
            targetPath = "";
        }

        if (string.Equals(currentPath, targetPath, StringComparison.OrdinalIgnoreCase))
        {
            return currentAdapter;
        }

        // Search in children
        var children = await currentAdapter.Children;
        foreach (var child in children.OfType<FolderTreeItemAdapter>())
        {
            var result = await this.FindFolderAdapterAsync(child, targetPath);
            if (result != null)
            {
                return result;
            }
        }

        return null;
    }

    /// <summary>
    /// Refreshes the project explorer tree by recreating the root node and reinitializing the tree view.
    /// Preserves the current selection from ContentBrowserState.
    /// </summary>
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

            this.projectRoot = new FolderTreeItemAdapter(this.logger, folder, projectInfo.Name, true)
            {
                IsExpanded = true,
            };

            // Reinitialize the tree UI
            await this.InitializeRootAsync(this.projectRoot, false).ConfigureAwait(true);

            // Allow selection changes to flow again for the explicit selection we apply next
            this.suppressTreeSelectionEvents = false;

            // Reapply previous selection if it still exists, prioritizing non-root paths
            var candidates = previousSelection
                .Where(p => !string.IsNullOrEmpty(p) && p != ".")
                .OrderBy(p => p, StringComparer.Ordinal)
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
    /// Handles tree selection changes and updates ContentBrowserState accordingly.
    /// This prevents duplicate history entries by using atomic operations.
    /// </summary>
    private void OnTreeSelectionChanged(object? sender, PropertyChangedEventArgs e)
    {
        Debug.WriteLine($"[ProjectLayoutViewModel] OnTreeSelectionChanged called: PropertyName={e.PropertyName}, isUpdatingFromState={this.isUpdatingFromState}, suppressTreeSelectionEvents={this.suppressTreeSelectionEvents}");

        // Only handle SelectedIndex changes for MultipleSelectionModel to avoid infinite loops
        // SelectedIndex changes when the selection changes, so we only need to listen to this one property
        if (this.isUpdatingFromState || this.suppressTreeSelectionEvents ||
            e.PropertyName != nameof(MultipleSelectionModel<ITreeItem>.SelectedIndex) ||
            this.SelectionModel is not MultipleSelectionModel<ITreeItem> multipleSelection)
        {
            Debug.WriteLine($"[ProjectLayoutViewModel] OnTreeSelectionChanged - early return. isUpdatingFromState={this.isUpdatingFromState}, suppressTreeSelectionEvents={this.suppressTreeSelectionEvents}, PropertyName={e.PropertyName}, SelectionModel type={this.SelectionModel?.GetType().Name}");
            return;
        }

        Debug.WriteLine($"[ProjectLayoutViewModel] Tree selection changed, updating ContentBrowserState. SelectedIndices count: {multipleSelection.SelectedIndices.Count}");
        this.logger.LogDebug("Tree selection changed, updating ContentBrowserState. SelectedIndices count: {Count}",
            multipleSelection.SelectedIndices.Count);

        // Get currently selected folder adapters from ALL selected indices
        var selectedFolders = multipleSelection.SelectedIndices
            .Select(index => this.ShownItems[index])
            .OfType<FolderTreeItemAdapter>()
            .Select(adapter => adapter.Folder.GetPathRelativeTo(contentBrowserState.ProjectRootPath))
            .ToList();

        Debug.WriteLine($"[ProjectLayoutViewModel] Selected folders: [{string.Join(", ", selectedFolders)}]");
        this.logger.LogDebug("Selected folders: [{Folders}]", string.Join(", ", selectedFolders));

        // Update ContentBrowserState
        Debug.WriteLine($"[ProjectLayoutViewModel] Updating ContentBrowserState with {selectedFolders.Count} selected folders");
        this.logger.LogDebug("Updating ContentBrowserState with {Count} selected folders", selectedFolders.Count);
        contentBrowserState.SetSelectedFolders(selectedFolders);

        Debug.WriteLine($"[ProjectLayoutViewModel] ContentBrowserState updated successfully");
    }

    /// <summary>
    /// Handles ContentBrowserState changes and updates tree selection accordingly.
    /// </summary>
    private async void OnContentBrowserStatePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        Debug.WriteLine($"[ProjectLayoutViewModel] OnContentBrowserStatePropertyChanged called: PropertyName={e.PropertyName}");

        if (e.PropertyName == nameof(ContentBrowserState.SelectedFolders))
        {
            Debug.WriteLine($"[ProjectLayoutViewModel] ContentBrowserState.SelectedFolders changed. New selection: [{string.Join(", ", contentBrowserState.SelectedFolders)}]");
            this.logger.LogDebug("ContentBrowserState.SelectedFolders changed. New selection: [{Folders}]",
                string.Join(", ", contentBrowserState.SelectedFolders));
            await this.UpdateTreeSelectionFromStateAsync();
        }
    }

    /// <summary>
    /// Updates tree selection to match ContentBrowserState.
    /// Uses atomic operations to prevent feedback loops and duplicate history entries.
    /// </summary>
    private async Task UpdateTreeSelectionFromStateAsync()
    {
        if (this.projectRoot == null)
        {
            Debug.WriteLine("[ProjectLayoutViewModel] UpdateTreeSelectionFromStateAsync - projectRoot is null, returning");
            return;
        }

        Debug.WriteLine("[ProjectLayoutViewModel] Updating tree selection from ContentBrowserState");
        this.logger.LogDebug("Updating tree selection from ContentBrowserState");

        try
        {
            this.isUpdatingFromState = true;
            Debug.WriteLine($"[ProjectLayoutViewModel] Set isUpdatingFromState = true");

            var selectedPaths = contentBrowserState.SelectedFolders.ToList();
            Debug.WriteLine($"[ProjectLayoutViewModel] Selected paths to sync: [{string.Join(", ", selectedPaths)}]");

            // First, clear all current selections in the tree items
            await this.ClearAllTreeItemSelections();
            Debug.WriteLine("[ProjectLayoutViewModel] Cleared all tree item selections");

            // Then set IsSelected=true for the items that should be selected
            foreach (var path in selectedPaths)
            {
                // Skip empty paths - they cause issues
                if (string.IsNullOrEmpty(path))
                {
                    Debug.WriteLine($"[ProjectLayoutViewModel] Skipping empty path");
                    continue;
                }

                var folderAdapter = await this.FindFolderAdapterAsync(this.projectRoot, path);
                if (folderAdapter != null)
                {
                    Debug.WriteLine($"[ProjectLayoutViewModel] Setting IsSelected=true for path: {path}");
                    folderAdapter.IsSelected = true;
                }
                else
                {
                    Debug.WriteLine($"[ProjectLayoutViewModel] Could not find folder adapter for path: {path}");
                }
            }

            Debug.WriteLine("[ProjectLayoutViewModel] Completed tree selection update from ContentBrowserState");
        }
        finally
        {
            this.isUpdatingFromState = false;
            Debug.WriteLine($"[ProjectLayoutViewModel] Set isUpdatingFromState = false");
        }
    }

    /// <summary>
    /// Clears the IsSelected property on all tree items.
    /// </summary>
    private async Task ClearAllTreeItemSelections()
    {
        if (this.projectRoot == null) return;

        await this.SetTreeItemSelectionRecursively(this.projectRoot, false);
    }

    /// <summary>
    /// Recursively sets the IsSelected property on tree items.
    /// </summary>
    private async Task SetTreeItemSelectionRecursively(FolderTreeItemAdapter adapter, bool isSelected)
    {
        adapter.IsSelected = isSelected;

        var children = await adapter.Children;
        foreach (var child in children.OfType<FolderTreeItemAdapter>())
        {
            await this.SetTreeItemSelectionRecursively(child, isSelected);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "The project manager service does not have a currently loaded project.")]
    private partial void LogNoCurrentProject();

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to preload project folders during ViewModel activation.")]
    private partial void LogPreloadingProjectFoldersError(Exception ex);
}
