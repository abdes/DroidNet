// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.Input;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Aura.Dialogs;
using DroidNet.Controls;
using DroidNet.Controls.Selection;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Oxygen.Assets.Filesystem;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Storage;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

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
    IDialogService dialogService,
    ViewModelToView vmToView,
    IMessenger messenger,
    IProjectAssetCatalog projectAssetCatalog,
    ILoggerFactory? loggerFactory)
    : DynamicTreeViewModel(loggerFactory), IRoutingAware
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<ProjectLayoutViewModel>() ??
                                      NullLoggerFactory.Instance.CreateLogger<ProjectLayoutViewModel>();

    private readonly ViewModelToView vmToView = vmToView;
    private readonly IMessenger messenger = messenger;
    private readonly IProjectAssetCatalog projectAssetCatalog = projectAssetCatalog;

    private IActiveRoute? activeRoute;
    private bool isUpdatingFromState;
    private ProjectRootTreeItemAdapter? projectRoot;
    private bool suppressTreeSelectionEvents;
    private bool isSubscribed;

    private ITreeItem? selectedItem;
    private bool canUnmountSelectedItem;
    private bool canRenameSelectedItem;
    private bool hasUnsavedChanges;

    /// <summary>
    ///     Raised when the UI should begin in-place rename for the selected item.
    ///     The view handles this request and triggers the DynamicTree in-place rename UI.
    /// </summary>
    public event EventHandler<ITreeItem>? RenameRequested;

    /// <summary>
    ///     Gets the currently selected tree item when there is exactly one selected item.
    /// </summary>
    public new ITreeItem? SelectedItem
    {
        get => this.selectedItem;
        private set => this.SetProperty(ref this.selectedItem, value);
    }

    /// <summary>
    ///     Gets a value indicating whether there are unsaved changes to the project mounts.
    /// </summary>
    public bool HasUnsavedChanges
    {
        get => this.hasUnsavedChanges;
        private set
        {
            if (this.SetProperty(ref this.hasUnsavedChanges, value))
            {
                this.SaveProjectMountsCommand.NotifyCanExecuteChanged();
            }
        }
    }

    /// <summary>
    ///     Gets a value indicating whether the current selection can be unmounted.
    /// </summary>
    public bool CanUnmountSelectedItem
    {
        get => this.canUnmountSelectedItem;
        private set
        {
            if (this.SetProperty(ref this.canUnmountSelectedItem, value))
            {
                this.UnmountSelectedItemCommand.NotifyCanExecuteChanged();
            }
        }
    }

    /// <summary>
    ///     Gets a value indicating whether the current selection can be renamed.
    /// </summary>
    public bool CanRenameSelectedItem
    {
        get => this.canRenameSelectedItem;
        private set
        {
            if (this.SetProperty(ref this.canRenameSelectedItem, value))
            {
                this.RenameSelectedItemCommand.NotifyCanExecuteChanged();
            }
        }
    }

    /// <inheritdoc />
    public async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        this.activeRoute = route;

        // Default selection mode for Project Explorer.
        this.SelectionMode = SelectionMode.Multiple;

        if (!this.isSubscribed)
        {
            // Subscribe to ContentBrowserState changes
            contentBrowserState.PropertyChanged += this.OnContentBrowserStatePropertyChanged;

            // Subscribe to navigation requests
            this.messenger.Register<NavigateToFolderRequestMessage>(this, (_, message) => _ = HandleNavigateRequestAsync(message));

            this.isSubscribed = true;
        }

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

        return;

        async Task HandleNavigateRequestAsync(NavigateToFolderRequestMessage message)
        {
            try
            {
                await this.NavigateToFolderAsync(message.Folder).ConfigureAwait(true);
            }
            catch (Exception ex)
            {
                this.logger.LogError(ex, "Failed to navigate to folder");
            }
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
            if (this.projectRoot is not null)
            {
                this.projectRoot.MountRenamed -= this.OnMountRenamed;
                this.projectRoot.Dispose();
            }

            this.projectRoot = new ProjectRootTreeItemAdapter(this.logger, storage, projectInfo, folder)
            {
                IsExpanded = true,
            };

            // Load persisted local folder mounts
            await this.LoadPersistedMountsAsync(storage, projectInfo).ConfigureAwait(true);

            this.projectRoot.MountRenamed += this.OnMountRenamed;

            // Ensure the root children are loading to avoid assertion in DoGetChildrenCount
            // when logging accesses ChildrenCount before the lazy loader is triggered.
            _ = this.projectRoot.Children;

            // Reinitialize the tree UI
            await this.InitializeRootAsync(this.projectRoot, skipRoot: false).ConfigureAwait(true);

            // Allow selection changes to flow again for the explicit selection we apply next
            this.suppressTreeSelectionEvents = false;

            // Reapply previous selection if it still exists, prioritizing non-root paths
            var candidates = previousSelection
                .Where(p => !string.IsNullOrEmpty(p) && !string.Equals(p, ".", StringComparison.Ordinal))
                .Order(StringComparer.Ordinal)
                .ToList();

            ITreeItem? target = null;
            if (candidates.Count > 0 && this.projectRoot is not null)
            {
                foreach (var path in candidates)
                {
                    var adapter = await FindFolderAdapterAsync(this.projectRoot, path).ConfigureAwait(true);
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
                this.SelectionModel?.SelectItem(target);
            }

            this.UpdateSelectionDerivedState();
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
        var folderAdapter = await FindFolderAdapterAsync(this.projectRoot, pathRelativeToProjectRoot)
            .ConfigureAwait(true);
        if (folderAdapter != null)
        {
            // Use the tree control's selection API to properly select the folder
            // The tree control will handle updating ContentBrowserState through FolderTreeItemAdapter
            this.SelectionModel?.SelectItem(folderAdapter);
        }
    }

    /// <summary>
    ///     Protected dispose pattern implementation.
    /// </summary>
    /// <param name="disposing">True if called from Dispose; false if called from finalizer.</param>
    protected override void Dispose(bool disposing)
    {
        base.Dispose(disposing);

        if (disposing)
        {
            this.messenger.UnregisterAll(this);
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
    ///     Recursively finds a folder adapter by its relative path.
    /// </summary>
    /// <param name="currentAdapter">The current adapter to search from.</param>
    /// <param name="targetPath">The target relative path to find.</param>
    /// <returns>The folder adapter if found, null otherwise.</returns>
    private static async Task<FolderTreeItemAdapter?> FindFolderAdapterAsync(
        TreeItemAdapter currentAdapter,
        string targetPath)
    {
        if (string.IsNullOrEmpty(targetPath) || string.Equals(targetPath, ".", StringComparison.Ordinal))
        {
            return currentAdapter as FolderTreeItemAdapter;
        }

        var normalizedPath = targetPath.Replace('\\', '/');
        var parts = normalizedPath.Split('/', StringSplitOptions.RemoveEmptyEntries);
        var current = currentAdapter;

        foreach (var part in parts)
        {
            if (!current.IsExpanded)
            {
                current.IsExpanded = true;
            }

            var children = await current.Children.ConfigureAwait(true);
            TreeItemAdapter? next = null;

            foreach (var child in children)
            {
                if (child is FolderTreeItemAdapter folderChild &&
                    string.Equals(folderChild.Folder.Name, part, StringComparison.OrdinalIgnoreCase))
                {
                    next = folderChild;
                    break;
                }
            }

            if (next is not null)
            {
                current = next;
            }
            else
            {
                return null;
            }
        }

        return current as FolderTreeItemAdapter;
    }

    /// <summary>
    ///     Recursively sets the IsSelected property on tree items.
    /// </summary>
    private static async Task SetTreeItemSelectionRecursively(TreeItemAdapter adapter, bool isSelected)
    {
        adapter.IsSelected = isSelected;

        var children = await adapter.Children.ConfigureAwait(true);
        foreach (var child in children.OfType<TreeItemAdapter>())
        {
            await SetTreeItemSelectionRecursively(child, isSelected).ConfigureAwait(true);
        }
    }

    [RelayCommand]
    private async Task MountKnownLocationAsync(KnownVirtualFolderMount kind)
    {
        if (this.projectRoot is null)
        {
            return;
        }

        var (mountPointName, projectRelativeBackingPath) = kind switch
        {
            KnownVirtualFolderMount.Cooked => ("Cooked", ".cooked"),
            KnownVirtualFolderMount.Imported => ("Imported", ".imported"),
            KnownVirtualFolderMount.Build => ("Build", ".build"),
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, message: "Unknown mount kind."),
        };

        try
        {
            var storage = projectManager.GetCurrentProjectStorageProvider();
            var mountRootLocation = storage.NormalizeRelativeTo(this.projectRoot.ProjectRootFolder.Location, projectRelativeBackingPath);
            var mountRootFolder = await storage.GetFolderFromPathAsync(mountRootLocation).ConfigureAwait(true);

            VirtualFolderMountTreeItemAdapter? mount = null;
            try
            {
                mount = new VirtualFolderMountTreeItemAdapter(
                    this.logger,
                    mountPointName,
                    mountRootFolder,
                    projectRelativeBackingPath,
                    VirtualFolderMountBackingPathKind.ProjectRelative);

                mount.PropertyChanged += this.OnMountPointPropertyChanged;

                if (await this.projectRoot.MountVirtualFolderAsync(mount).ConfigureAwait(true))
                {
                    var mountedItem = mount;
                    mount = null;

                    await this.InsertItemAsync(mountedItem, this.projectRoot, this.projectRoot.ChildrenCount).ConfigureAwait(true);
                    await this.ExpandItemAsync(mountedItem).ConfigureAwait(true);

                    // Index the newly mounted folder so it shows up in asset browsing queries.
                    _ = this.projectAssetCatalog.AddFolderAsync(mountRootFolder, mountedItem.VirtualRootPath.TrimStart('/'));

                    this.HasUnsavedChanges = true;
                }
            }
            finally
            {
                mount?.Dispose();
            }
        }
        catch (Exception ex)
        {
            this.LogPreloadingProjectFoldersError(ex);
        }
    }

    [RelayCommand]
    private async Task MountLocalFolderAsync()
    {
        if (this.projectRoot is null)
        {
            return;
        }

        try
        {
            var existingNames = await this.GetExistingMountPointNamesAsync().ConfigureAwait(true);
            var vm = new LocalFolderMountDialogViewModel(dialogService, existingNames);

            // Resolve view for the dialog using the locally-scoped ViewModelToView converter.
            if (this.vmToView.Convert(vm, typeof(object), parameter: null, language: System.Globalization.CultureInfo.CurrentUICulture.Name) is not UIElement view)
            {
                throw new InvalidOperationException("VmToViewConverter returned null UIElement for LocalFolderMountDialogViewModel");
            }

            // Ensure the view's ViewModel property is set (IViewFor<T>).
            if (view is DroidNet.Mvvm.IViewFor vf)
            {
                vf.ViewModel = vm;
            }

            var spec = new DialogSpec("Mount Local Folder", view)
            {
                PrimaryButtonText = "OK",
                SecondaryButtonText = "Cancel",
                CloseButtonText = string.Empty,
                DefaultButton = DialogButton.Primary,
            };

            var button = await dialogService.ShowAsync(spec).ConfigureAwait(true);
            var definition = button == DialogButton.Primary ? vm.Result : null;
            if (definition is null)
            {
                return;
            }

            var storage = projectManager.GetCurrentProjectStorageProvider();
            var mountRootFolder = await storage.GetFolderFromPathAsync(definition.AbsoluteFolderPath).ConfigureAwait(true);

            VirtualFolderMountTreeItemAdapter? mount = null;
            try
            {
                mount = new VirtualFolderMountTreeItemAdapter(
                    this.logger,
                    definition.MountPointName,
                    mountRootFolder,
                    definition.AbsoluteFolderPath,
                    VirtualFolderMountBackingPathKind.Absolute);

                mount.PropertyChanged += this.OnMountPointPropertyChanged;

                if (await this.projectRoot.MountVirtualFolderAsync(mount).ConfigureAwait(true))
                {
                    var mountedItem = mount;
                    mount = null;

                    await this.InsertItemAsync(mountedItem, this.projectRoot, this.projectRoot.ChildrenCount).ConfigureAwait(true);
                    await this.ExpandItemAsync(mountedItem).ConfigureAwait(true);

                    // Index the newly mounted folder
                    _ = this.projectAssetCatalog.AddFolderAsync(mountRootFolder, mountedItem.VirtualRootPath.TrimStart('/'));

                    this.HasUnsavedChanges = true;
                }
            }
            finally
            {
                mount?.Dispose();
            }
        }
        catch (Exception ex)
        {
            this.LogPreloadingProjectFoldersError(ex);
        }
    }

    [RelayCommand(CanExecute = nameof(CanUnmountSelectedItem))]
    private async Task UnmountSelectedItemAsync()
    {
        if (this.projectRoot is null)
        {
            return;
        }

        string? mountPointName = null;
        ITreeItem? itemToRemove = null;

        if (this.SelectedItem is VirtualFolderMountTreeItemAdapter virtualMount)
        {
            mountPointName = virtualMount.MountPointName;
            itemToRemove = virtualMount;
        }
        else if (this.SelectedItem is AuthoringMountPointTreeItemAdapter authoringMount)
        {
            mountPointName = authoringMount.MountPoint.Name;
            itemToRemove = authoringMount;
        }

        if (mountPointName is null || itemToRemove is null)
        {
            return;
        }

        try
        {
            // Try to unmount as virtual folder first (covers local mounts and newly added mounts)
            var unmounted = await this.projectRoot.UnmountVirtualFolderAsync(mountPointName).ConfigureAwait(true);

            // If not found in virtual mounts, it might be an authoring mount loaded from project info
            if (!unmounted && this.SelectedItem is AuthoringMountPointTreeItemAdapter)
            {
                // Authoring mounts are direct children, so we can just remove them from the tree
                // and then SaveProjectMountsAsync will handle the persistence update (by omitting it).
                unmounted = true;
            }

            if (unmounted)
            {
                if (itemToRemove is INotifyPropertyChanged observable)
                {
                    observable.PropertyChanged -= this.OnMountPointPropertyChanged;
                }

                await this.RemoveItemAsync(itemToRemove, updateSelection: true).ConfigureAwait(true);

                // This will rebuild the list of mounts from the current tree state, effectively removing the unmounted one.
                this.HasUnsavedChanges = true;
            }

            // Ensure selection remains valid.
            this.SelectionModel?.SelectItem(this.projectRoot);
            this.UpdateSelectionDerivedState();
        }
        catch (Exception ex)
        {
            this.LogPreloadingProjectFoldersError(ex);
        }
    }

    [RelayCommand(CanExecute = nameof(CanRenameSelectedItem))]
    private void RenameSelectedItem()
    {
        if (this.SelectedItem is null)
        {
            return;
        }

        // Rename is performed via the DynamicTree in-place rename UI.
        this.RenameRequested?.Invoke(this, this.SelectedItem);
    }

    private void RestoreState()
    {
        Debug.Assert(this.activeRoute is not null, "should have an active route");

        this.LogRestoreStateStart();

        var selectedFolders = RouteStateMapping.GetSelectedFolders(this.activeRoute);
        contentBrowserState.SelectedFolders.Clear();

        foreach (var relativePath in selectedFolders)
        {
            this.LogRestoreStateAddFolder(relativePath);
            _ = contentBrowserState.SelectedFolders.Add(relativePath);
        }

        this.LogRestoreStateFinal(string.Join(", ", contentBrowserState.SelectedFolders));
    }

    private void OnMountRenamed(object? sender, VirtualFolderMountTreeItemAdapter mount)
        => this.HasUnsavedChanges = true;

    [RelayCommand(CanExecute = nameof(HasUnsavedChanges))]
    private async Task SaveProjectMountsAsync()
    {
        var project = projectManager.CurrentProject;
        if (project is null || this.projectRoot is null)
        {
            return;
        }

        var projectInfo = project.ProjectInfo;
        projectInfo.LocalFolderMounts.Clear();
        projectInfo.AuthoringMounts.Clear();

        // Rebuild AuthoringMounts and LocalFolderMounts from the current tree state.
        // We need to iterate over ALL mount items in the tree, not just VirtualFolderMounts.
        // The tree contains both AuthoringMountPointTreeItemAdapter and VirtualFolderMountTreeItemAdapter.
        var children = await this.projectRoot.Children.ConfigureAwait(true);
        foreach (var child in children)
        {
            string mountName;
            string backingPath;
            bool isProjectRelative;
            if (child is VirtualFolderMountTreeItemAdapter virtualMount)
            {
                mountName = virtualMount.MountPointName;
                backingPath = virtualMount.RootFolder.Location;
                isProjectRelative = virtualMount.BackingPathKind == VirtualFolderMountBackingPathKind.ProjectRelative;
            }
            else if (child is AuthoringMountPointTreeItemAdapter authoringMount)
            {
                mountName = authoringMount.MountPoint.Name;
                backingPath = authoringMount.RootFolder.Location;

                // Authoring mounts are by definition project relative, but we need to re-calculate the relative path
                // or use the one from the mount point if we trust it hasn't changed.
                // Let's treat it as absolute for now and let the logic below re-relativize it to be safe/consistent.
                isProjectRelative = false;
            }
            else
            {
                continue;
            }

            string? relativePath = null;

            if (isProjectRelative)
            {
                // If it was already marked relative (e.g. .cooked), keep it relative path
                // Note: VirtualFolderMountTreeItemAdapter stores the relative path in BackingPath if kind is ProjectRelative
                if (child is VirtualFolderMountTreeItemAdapter vm)
                {
                    relativePath = vm.BackingPath;
                }
            }

            if (relativePath == null)
            {
                // Try to make it relative if it is inside the project root
                var projectRootPath = this.projectRoot.ProjectRootFolder.Location;

                try
                {
                    var rel = System.IO.Path.GetRelativePath(projectRootPath, backingPath);
                    if (!rel.StartsWith("..", StringComparison.Ordinal) && !System.IO.Path.IsPathRooted(rel))
                    {
                        relativePath = rel.Replace('\\', '/');
                    }
                }
                catch (Exception)
                {
                    // Ignore path errors, treat as absolute
                }
            }

            if (relativePath != null)
            {
                projectInfo.AuthoringMounts.Add(new ProjectMountPoint(mountName, relativePath, child.IsExpanded));
            }
            else
            {
                projectInfo.LocalFolderMounts.Add(new LocalFolderMount(mountName, backingPath, child.IsExpanded));
            }
        }

        await projectManager.SaveProjectInfoAsync(projectInfo).ConfigureAwait(true);
        this.HasUnsavedChanges = false;
    }

    private async Task LoadPersistedMountsAsync(IStorageProvider storage, IProjectInfo projectInfo)
    {
        if (this.projectRoot is null)
        {
            return;
        }

        foreach (var localMount in projectInfo.LocalFolderMounts)
        {
            try
            {
                var mountRootFolder = await storage.GetFolderFromPathAsync(localMount.AbsolutePath).ConfigureAwait(true);
                VirtualFolderMountTreeItemAdapter? mount = null;
                try
                {
                    mount = new VirtualFolderMountTreeItemAdapter(
                        this.logger,
                        localMount.Name,
                        mountRootFolder,
                        localMount.AbsolutePath,
                        VirtualFolderMountBackingPathKind.Absolute)
                    {
                        IsExpanded = localMount.IsExpanded,
                    };

                    mount.PropertyChanged += this.OnMountPointPropertyChanged;

                    if (await this.projectRoot.MountVirtualFolderAsync(mount).ConfigureAwait(true))
                    {
                        var mountedItem = mount;
                        mount = null;

                        // Index the newly mounted folder
                        _ = this.projectAssetCatalog.AddFolderAsync(mountRootFolder, mountedItem.VirtualRootPath.TrimStart('/'));

                        if (this.projectRoot.AreChildrenLoaded)
                        {
                            await this.InsertItemAsync(mountedItem, this.projectRoot, this.projectRoot.ChildrenCount).ConfigureAwait(true);
                        }
                    }
                }
                finally
                {
                    mount?.Dispose();
                }
            }
            catch (Exception ex)
            {
                this.logger.LogError(ex, "Failed to load local folder mount {Name}", localMount.Name);
            }
        }
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
                this.projectRoot = new ProjectRootTreeItemAdapter(
                    this.logger,
                    storage,
                    projectInfo,
                    folder)
                {
                    IsExpanded = true,
                };

                // Load persisted local folder mounts
                await this.LoadPersistedMountsAsync(storage, projectInfo).ConfigureAwait(true);

                this.projectRoot.MountRenamed += this.OnMountRenamed;
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

        // Ensure the root children are loading to avoid assertion in DoGetChildrenCount
        // when logging accesses ChildrenCount before the lazy loader is triggered.
        _ = this.projectRoot.Children;

        // Initialize the project tree
        await this.InitializeRootAsync(this.projectRoot, skipRoot: false).ConfigureAwait(true);

        // Attach property change listeners to Authoring Mounts (which are loaded by InitializeRootAsync -> LoadChildren)
        // Virtual Folder Mounts are already handled in LoadPersistedMountsAsync or Mount... methods.
        var children = await this.projectRoot.Children.ConfigureAwait(true);
        foreach (var child in children)
        {
            if (child is AuthoringMountPointTreeItemAdapter authoringMount)
            {
                authoringMount.PropertyChanged += this.OnMountPointPropertyChanged;
            }
        }
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

        // Get currently selected folder adapters from ALL selected indices.
        // For now, only folder nodes and the project root contribute to ContentBrowserState.SelectedFolders.
        var selectedFolders = multipleSelection.SelectedIndices
            .Select(this.GetShownItemAt)
            .Select(this.GetVirtualPath)
            .Where(static p => !string.IsNullOrEmpty(p))
            .Select(static p => p!)
            .ToList();

        this.LogSelectedFolders(string.Join(", ", selectedFolders));

        // Update ContentBrowserState
        this.LogUpdatingContentBrowserState(selectedFolders.Count);
        contentBrowserState.SetSelectedFolders(selectedFolders);

        this.LogContentBrowserStateUpdated();

        this.UpdateSelectionDerivedState();
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

                ITreeItem? target;
                if (string.Equals(path, "/", StringComparison.Ordinal) || string.Equals(path, ".", StringComparison.Ordinal))
                {
                    target = this.projectRoot;
                }
                else if (VirtualPath.IsCanonicalAbsolute(path))
                {
                    // Find the adapter by virtual path
                    target = await this.FindAdapterByVirtualPathAsync(path).ConfigureAwait(true);
                }
                else
                {
                    // Fallback for project-relative OS paths (legacy/non-authoring)
                    target = await FindFolderAdapterAsync(this.projectRoot, path).ConfigureAwait(true);
                }

                if (target is TreeItemAdapter adapter)
                {
                    this.LogSettingIsSelected(path);
                    adapter.IsSelected = true;
                }
                else
                {
                    this.LogFolderAdapterNotFound(path);
                }
            }

            this.LogUpdateTreeSelectionCompleted();

            this.UpdateSelectionDerivedState();
        }
        finally
        {
            this.isUpdatingFromState = false;
            this.LogSetIsUpdatingFromState(value: false);
        }
    }

    private void UpdateSelectionDerivedState()
    {
        if (this.SelectionModel is not MultipleSelectionModel<ITreeItem> multipleSelection)
        {
            this.SelectedItem = null;
            this.CanUnmountSelectedItem = false;
            this.CanRenameSelectedItem = false;
            return;
        }

        this.SelectedItem = multipleSelection.SelectedIndices.Count == 1
            ? this.GetShownItemAt(multipleSelection.SelectedIndices[0])
            : null;

        this.CanUnmountSelectedItem = this.SelectedItem is VirtualFolderMountTreeItemAdapter or AuthoringMountPointTreeItemAdapter;

        this.CanRenameSelectedItem = this.SelectedItem is FolderTreeItemAdapter
            or ProjectRootTreeItemAdapter
            or AuthoringMountPointTreeItemAdapter
            or VirtualFolderMountTreeItemAdapter;
    }

    private async Task<ITreeItem?> FindAdapterByVirtualPathAsync(string virtualPath)
    {
        if (this.projectRoot == null)
        {
            return null;
        }

        // virtualPath is guaranteed to be canonical absolute and not "/"
        var segments = virtualPath.Split('/', StringSplitOptions.RemoveEmptyEntries);
        if (segments.Length == 0)
        {
            return null;
        }

        var mountName = segments[0];
        var children = await this.projectRoot.Children.ConfigureAwait(true);

        TreeItemAdapter? mount = children.OfType<AuthoringMountPointTreeItemAdapter>()
            .FirstOrDefault(m => string.Equals(m.MountPoint.Name, mountName, StringComparison.Ordinal));
        mount ??= children.OfType<VirtualFolderMountTreeItemAdapter>()
            .FirstOrDefault(m => string.Equals(m.MountPointName, mountName, StringComparison.Ordinal));

        if (mount == null)
        {
            return null;
        }

        if (segments.Length == 1)
        {
            return mount;
        }

        // Find subfolder within the mount
        var relativePath = string.Join('/', segments.Skip(1));
        return await FindFolderAdapterAsync(mount, relativePath).ConfigureAwait(true);
    }

    private async Task<IReadOnlyCollection<string>> GetExistingMountPointNamesAsync()
    {
        var result = new HashSet<string>(StringComparer.Ordinal);

        if (this.projectRoot is null)
        {
            return result;
        }

        // Ensure children are loaded.
        var children = await this.projectRoot.Children.ConfigureAwait(true);

        foreach (var child in children)
        {
            switch (child)
            {
                case AuthoringMountPointTreeItemAdapter authoring:
                    _ = result.Add(authoring.MountPoint.Name);
                    break;
                case VirtualFolderMountTreeItemAdapter virtualMount:
                    _ = result.Add(virtualMount.MountPointName);
                    break;
            }
        }

        return result;
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

    private string? GetVirtualPath(ITreeItem item)
        => item switch
        {
            ProjectRootTreeItemAdapter => "/",
            AuthoringMountPointTreeItemAdapter authoring => authoring.VirtualRootPath,
            VirtualFolderMountTreeItemAdapter virtualMount => virtualMount.VirtualRootPath,
            FolderTreeItemAdapter folder => this.GetFolderVirtualPath(folder),
            _ => null,
        };

    private string? GetFolderVirtualPath(FolderTreeItemAdapter folder)
    {
        // Find the first ancestor that is a mount or root
        var current = folder.Parent;
        while (current != null)
        {
            if (current is AuthoringMountPointTreeItemAdapter authoringAncestor)
            {
                var relative = folder.Folder.GetPathRelativeTo(authoringAncestor.RootFolder.Location);
                return VirtualPath.Combine(authoringAncestor.VirtualRootPath, relative);
            }

            if (current is VirtualFolderMountTreeItemAdapter virtualAncestor)
            {
                var relative = folder.Folder.GetPathRelativeTo(virtualAncestor.RootFolder.Location);
                return VirtualPath.Combine(virtualAncestor.VirtualRootPath, relative);
            }

            if (current is ProjectRootTreeItemAdapter)
            {
                return folder.Folder.GetPathRelativeTo(contentBrowserState.ProjectRootPath);
            }

            current = current.Parent;
        }

        return folder.Folder.GetPathRelativeTo(contentBrowserState.ProjectRootPath);
    }

    private void OnMountPointPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(ITreeItem.IsExpanded), StringComparison.Ordinal))
        {
            this.HasUnsavedChanges = true;
        }
    }
}
