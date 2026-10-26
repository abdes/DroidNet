// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.Dialogs;
using DroidNet.Controls;
using DroidNet.Controls.Selection;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Storage;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>
///     The Content Folders pane of the Content Explorer: the tree of the project's content folders and mounts.
///     Acts as a mediator between the DynamicTree control and ContentExplorerState.
/// </summary>
/// <param name="session">The project session.</param>
/// <param name="storage">The storage provider.</param>
/// <param name="contentExplorerState">The state of the content explorer.</param>
/// <param name="dialogService">Shows the pane's dialogs.</param>
/// <param name="vmToView">Resolves the views of the pane's dialogs.</param>
/// <param name="loggerFactory">
///     Optional factory for creating loggers. If provided, enables detailed logging of the recognition
///     process. If <see langword="null" />, logging is disabled.
/// </param>
/// <param name="mountChanges">Applies a confirmed change of the content mounts.</param>
/// <param name="pane">Takes the assets pane's requests to show or create a folder while this pane lives.</param>
/// <param name="publications">Reads the project's selected cook publication: its output folders and products.</param>
/// <param name="catalog">The asset catalog, whose changes may bring new cooked folders.</param>
/// <param name="folders">Reports the folders created, deleted or renamed under the authoring roots.</param>
/// <param name="hostingContext">Brings catalog notifications to the UI thread.</param>
/// <param name="engineContent">The engine content pak, shown as a cooked mount.</param>
/// <param name="relocation">Renames, moves and copies folders with their references; without it only empty folders rename.</param>
/// <param name="relocationService">Reports the folders the editor moved or deleted; absent where the editor cannot relocate.</param>
public partial class ContentFoldersViewModel(
    IProjectSession session,
    IStorageProvider storage,
    ContentExplorerState contentExplorerState,
    IDialogService dialogService,
    ViewModelToView vmToView,
    IContentMountChanges mountChanges,
    ContentFoldersPane pane,
    Oxygen.Editor.ContentPipeline.Publication.ICookPublicationReader publications,
    IAssetCatalog catalog,
    IAssetFolderSource folders,
    HostingContext hostingContext,
    Oxygen.Editor.ContentPipeline.IEngineContentPak engineContent,
    ILoggerFactory? loggerFactory,
    Relocation.IAssetRelocationWorkflow? relocation = null,
    IAssetRelocationService? relocationService = null)
    : DynamicTreeViewModel(loggerFactory), IRoutingAware
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<ContentFoldersViewModel>() ?? NullLogger<ContentFoldersViewModel>.Instance;

    private readonly ViewModelToView vmToView = vmToView;

    private IActiveRoute? activeRoute;
    private bool isUpdatingFromState;
    private ProjectRootTreeItemAdapter? projectRoot;
    private bool suppressTreeSelectionEvents;
    private bool isSubscribed;
    private IDisposable? catalogSubscription;
    private IDisposable? folderSubscription;
    private int publishedFoldersCheckQueued;
    private int folderReloadQueued;

    private ITreeItem? selectedItem;
    private bool hasUnsavedChanges;

    /// <summary>
    ///     Raised when the UI should begin in-place rename for the selected item.
    ///     The view handles this request and triggers the DynamicTree in-place rename UI.
    /// </summary>
    public event EventHandler<ProjectItemRenameRequestedEventArgs>? RenameRequested;

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

    /// <inheritdoc />
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The UI operation boundary reports failures while keeping the browser and other mounted content usable.")]
    public async Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        this.activeRoute = route;

        // Default selection mode for the Content Folders pane.
        this.SelectionMode = SelectionMode.Multiple;

        if (!this.isSubscribed)
        {
            // Subscribe to ContentExplorerState changes
            contentExplorerState.PropertyChanged += this.OnContentExplorerStatePropertyChanged;

            // The assets pane asks this pane to show and create folders.
            pane.Attach(this);
            this.catalogSubscription = catalog.Changes.Subscribe(_ => this.OnCatalogChanged());
            this.folderSubscription = folders.FolderChanges.Subscribe(_ => this.OnFoldersChanged());
            relocationService?.FilesChanged += this.OnFilesChanged;
            this.SelectionSettled += this.OnTreeSelectionSettled;

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
    }

    /// <summary>
    ///     Refreshes the project explorer tree by recreating the root node and reinitializing the tree view.
    ///     Preserves the current selection from ContentExplorerState.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous refresh operation.</returns>
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "pre-loading happens during route activation and we cannot report exceptions in that stage")]
    public Task RefreshTreeAsync() => this.ReloadMountTreeAsync();

    /// <summary>
    ///     Navigates to and selects the specified folder in the tree.
    ///     This is the entry point for programmatic navigation from other ViewModels.
    /// </summary>
    /// <param name="folder">The folder to navigate to and select.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous refresh operation.</returns>
    public async Task NavigateToFolderAsync(IFolder folder)
    {
        ArgumentNullException.ThrowIfNull(folder);
        var root = this.projectRoot;
        var version = ++this.selectionRequestVersion;
        if (root is null || this.selectionDisposed)
        {
            return;
        }

        var selection = await this.ResolveFolderNavigationAsync(root, folder).ConfigureAwait(true);
        if (selection is not null && this.IsSelectionRequestCurrent(version, root))
        {
            contentExplorerState.SetSelectedFolders(selection);
            await this.UpdateTreeSelectionFromStateAsync().ConfigureAwait(true);
        }
    }

    /// <summary>Identifies derived roots retained as project-relative virtual mounts.</summary>
    /// <param name="mount">The persisted mount declaration.</param>
    /// <returns>Whether the mount is a derived project root.</returns>
    internal static bool IsPersistedProjectRelativeVirtualMount(ProjectMountPoint mount)
    {
        var relativePath = mount.RelativePath.Trim().Replace('\\', '/').Trim('/');
        return string.Equals(relativePath, ".cooked", StringComparison.OrdinalIgnoreCase)
               || string.Equals(relativePath, ".imported", StringComparison.OrdinalIgnoreCase)
               || string.Equals(relativePath, ".build", StringComparison.OrdinalIgnoreCase);
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
            this.selectionDisposed = true;
            this.selectionRequestVersion++;
            pane.Detach(this);
            this.catalogSubscription?.Dispose();
            this.folderSubscription?.Dispose();
            relocationService?.FilesChanged -= this.OnFilesChanged;
            this.SelectionSettled -= this.OnTreeSelectionSettled;
            contentExplorerState.PropertyChanged -= this.OnContentExplorerStatePropertyChanged;
            this.projectRoot?.Dispose();
        }

        // No unmanaged resources to clean up.
    }

    /// <inheritdoc />
    protected override void OnSelectionModelChanged(SelectionModel<ITreeItem>? oldValue)
    {
        base.OnSelectionModelChanged(oldValue);

        // Unsubscribe from old selection model
        oldValue?.PropertyChanged -= this.OnTreeSelectionChanged;

        // Subscribe to new selection model
        this.SelectionModel?.PropertyChanged += this.OnTreeSelectionChanged;
    }

    /// <summary>
    ///     Recursively finds a folder adapter by its relative path.
    /// </summary>
    /// <param name="currentAdapter">The current adapter to search from.</param>
    /// <param name="targetPath">The target relative path to find.</param>
    /// <returns>The folder adapter if found, null otherwise.</returns>
    private static async Task<TreeItemAdapter?> FindFolderAdapterAsync(
        TreeItemAdapter currentAdapter,
        string targetPath)
    {
        if (string.IsNullOrEmpty(targetPath) || string.Equals(targetPath, ".", StringComparison.Ordinal))
        {
            return currentAdapter;
        }

        var normalizedPath = targetPath.Replace('\\', '/');
        var parts = normalizedPath.Split('/', StringSplitOptions.RemoveEmptyEntries);
        var current = currentAdapter;

        foreach (var part in parts)
        {
            var children = await current.Children.ConfigureAwait(true);
            TreeItemAdapter? next = null;

            foreach (var child in children)
            {
                if (child is TreeItemAdapter folderChild && (child is FolderTreeItemAdapter or CookedFolderTreeItemAdapter)
                    && string.Equals(folderChild.Label, part, StringComparison.OrdinalIgnoreCase))
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

        return current;
    }

    [RelayCommand(CanExecute = nameof(CanChangeMounts))]
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The UI operation boundary reports failures while keeping the browser and other mounted content usable.")]
    private async Task MountKnownLocationAsync(KnownVirtualFolderMount kind)
    {
        var expected = session.Context;
        var candidate = this.GetActiveProjectInfo();
        var (name, path) = kind switch
        {
            KnownVirtualFolderMount.Cooked => ("Cooked", ".cooked"),
            KnownVirtualFolderMount.Imported => ("Imported", ".imported"),
            KnownVirtualFolderMount.Build => ("Build", ".build"),
            _ => throw new ArgumentOutOfRangeException(nameof(kind)),
        };
        var existing = candidate.AuthoringMounts.FirstOrDefault(mount => string.Equals(mount.RelativePath, path, StringComparison.OrdinalIgnoreCase));
        if (existing is not null)
        {
            await this.ReloadMountTreeAsync().ConfigureAwait(true);
            contentExplorerState.SetSelectedFolders([AssetPath.Create(existing.Name)]);
            return;
        }

        candidate.AuthoringMounts.Add(new(name, path));
        await this.ApplyMountCandidateAsync(expected, candidate).ConfigureAwait(true);
        if (session.Context.AuthoringMounts.Any(mount => string.Equals(mount.Name, name, StringComparison.Ordinal) && string.Equals(mount.RelativePath, path, StringComparison.Ordinal)))
        {
            contentExplorerState.SetSelectedFolders([AssetPath.Create(name)]);
        }
    }

    [RelayCommand(CanExecute = nameof(CanChangeMounts))]
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The UI operation boundary reports failures while keeping the browser and other mounted content usable.")]
    private async Task MountLocalFolderAsync()
    {
        var expected = session.Context;
        var candidate = this.GetActiveProjectInfo();
        var names = await this.GetExistingMountPointNamesAsync().ConfigureAwait(true);
        var model = new LocalFolderMountDialogViewModel(dialogService, names);
        if (!await this.ShowLocalMountDialogAsync(model).ConfigureAwait(true) || model.Result is not { } definition)
        {
            return;
        }

        var folder = FileSystemPath.From(definition.AbsoluteFolderPath);
        var relative = GetRelativeMountPath(expected.ProjectRoot, folder);
        var isCooked = await storage.DocumentExistsAsync(Path.Combine(definition.AbsoluteFolderPath, "container.index.bin")).ConfigureAwait(true);
        if (relative is not null && !isCooked)
        {
            candidate.AuthoringMounts.Add(new(definition.MountPointName, relative));
        }
        else
        {
            candidate.LocalFolderMounts.Add(new(definition.MountPointName, folder));
        }

        await this.ApplyMountCandidateAsync(expected, candidate).ConfigureAwait(true);
    }

    private void RestoreState()
    {
        Debug.Assert(this.activeRoute is not null, "should have an active route");

        this.LogRestoreStateStart();

        var selectedFolders = RouteStateMapping.GetSelectedFolders(this.activeRoute);
        foreach (var folder in selectedFolders)
        {
            this.LogRestoreStateAddFolder(folder);
        }

        // Important: apply selection via ContentExplorerState so that PropertyChanged is raised.
        // The assets pane refreshes itself on SelectedFolders PropertyChanged.
        contentExplorerState.SetSelectedFolders(selectedFolders);

        this.LogRestoreStateFinal(contentExplorerState.SelectedFolders);
    }

    private void OnMountRenamed(object? sender, MountRenamedEventArgs e)
    {
        if (!this.IsApplyingMounts)
        {
            this.HasUnsavedChanges = true;
            this.PendingMountChange = this.SaveProjectMountsAsync();
        }
    }

    [RelayCommand(CanExecute = nameof(HasUnsavedChanges))]
    private async Task SaveProjectMountsAsync()
    {
        var activeProject = session.Context;
        var projectInfo = this.GetActiveProjectInfo();
        if (this.projectRoot is null)
        {
            return;
        }

        projectInfo.LocalFolderMounts.Clear();
        projectInfo.AuthoringMounts.Clear();

        var children = await this.projectRoot.Children.ConfigureAwait(true);
        foreach (var child in children)
        {
            this.AddMountToProjectInfo(projectInfo, child);
        }

        RemapPriorityNames(activeProject, projectInfo);
        await this.ApplyMountCandidateAsync(activeProject, projectInfo).ConfigureAwait(true);
    }

    private async Task LoadPersistedMountsAsync(ProjectRootTreeItemAdapter root, ProjectInfo projectInfo, ProjectContext context)
    {
        foreach (var mount in projectInfo.AuthoringMounts.Where(IsPersistedProjectRelativeVirtualMount))
        {
            await this.RestoreMountAsync(root, context, mount.Name, mount.RelativePath, isProjectRelative: true, mount.IsExpanded).ConfigureAwait(true);
        }

        foreach (var mount in projectInfo.LocalFolderMounts)
        {
            await this.RestoreMountAsync(root, context, mount.Name, mount.Root.FullPath, isProjectRelative: false, mount.IsExpanded).ConfigureAwait(true);
        }

        await this.MountPaksAsync(root, projectInfo).ConfigureAwait(true);
    }

    private Task PreloadRecentTemplatesAsync() => this.ReloadMountTreeAsync();

    [RelayCommand]
    private Task LoadProjectAsync() => this.ReloadMountTreeAsync();

    private ProjectInfo GetActiveProjectInfo()
    {
        var context = session.Context;

        var projectInfo = new ProjectInfo(
            context.ProjectId,
            context.Name,
            context.Category,
            context.ProjectRoot.FullPath,
            context.Thumbnail)
        {
            DefaultSceneId = context.DefaultSceneId,
            Settings = session.Settings,
        };
        foreach (var mount in context.AuthoringMounts)
        {
            projectInfo.AuthoringMounts.Add(mount);
        }

        foreach (var mount in context.LocalFolderMounts)
        {
            projectInfo.LocalFolderMounts.Add(mount);
        }

        foreach (var mount in context.PakFileMounts)
        {
            projectInfo.PakFileMounts.Add(mount);
        }

        foreach (var source in context.CookedContentOrder)
        {
            projectInfo.CookedContentOrder.Add(source);
        }

#if DEBUG
        Debug.Assert(
            projectInfo.Location is not null,
            "current project must be set, have a valid ProjectInfo and a valid Location");
#endif

        return projectInfo;
    }

    /// <summary>
    ///     Handles tree selection changes and updates ContentExplorerState accordingly.
    ///     This prevents duplicate history entries by using atomic operations.
    /// </summary>
    private void OnTreeSelectionChanged(object? sender, PropertyChangedEventArgs e)
    {
        this.LogTreeSelectionChanged(e.PropertyName, this.isUpdatingFromState, this.suppressTreeSelectionEvents);

        // Only handle SelectedIndex changes for MultipleSelectionModel to avoid infinite loops
        // SelectedIndex changes when the selection changes, so we only need to listen to this one property
        // A selection transaction publishes once, when it settles, with its final membership.
        if (this.isUpdatingFromState
            || this.suppressTreeSelectionEvents
            || this.IsSelectionBatchActive
            || !string.Equals(e.PropertyName, nameof(MultipleSelectionModel<>.SelectedIndex), StringComparison.Ordinal)
            || this.SelectionModel is not MultipleSelectionModel<ITreeItem> multipleSelection)
        {
            this.LogTreeSelectionChangedEarlyReturn(
                this.isUpdatingFromState,
                this.suppressTreeSelectionEvents,
                e.PropertyName,
                this.SelectionModel);
            return;
        }

        this.PublishTreeSelection(multipleSelection);
    }

    private void OnTreeSelectionSettled(object? sender, EventArgs e)
    {
        if (this.isUpdatingFromState
            || this.suppressTreeSelectionEvents
            || this.SelectionModel is not MultipleSelectionModel<ITreeItem> multipleSelection)
        {
            return;
        }

        this.PublishTreeSelection(multipleSelection);
    }

    private void PublishTreeSelection(MultipleSelectionModel<ITreeItem> multipleSelection)
    {
        this.LogTreeSelectionChangedUpdatingState(multipleSelection.SelectedIndices.Count);

        // Get currently selected folder adapters from ALL selected indices.
        // Folder and mount rows select their folder; the project's row selects no folder, which browses them all.
        var selectedFolders = multipleSelection.SelectedIndices
            .Select(this.GetShownItemAt)
            .Select(this.GetVirtualPath)
            .OfType<AssetPath>()
            .ToList();

        this.LogSelectedFolders(selectedFolders);

        // Update ContentExplorerState
        this.LogUpdatingContentExplorerState(selectedFolders.Count);
        contentExplorerState.SetSelectedFolders(selectedFolders);

        this.LogContentExplorerStateUpdated();

        this.UpdateSelectionDerivedState();
    }

    /// <summary>
    ///     Handles ContentExplorerState changes and updates tree selection accordingly.
    /// </summary>
    private async void OnContentExplorerStatePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        this.LogContentExplorerStatePropertyChanged(e.PropertyName);

        if (string.Equals(e.PropertyName, nameof(ContentExplorerState.SelectedFolders), StringComparison.Ordinal))
        {
            this.LogContentExplorerStateSelectedFoldersChanged(contentExplorerState.SelectedFolders);
            await this.UpdateTreeSelectionFromStateAsync().ConfigureAwait(true);
        }
    }

    private void UpdateSelectionDerivedState()
    {
        if (this.SelectionModel is not MultipleSelectionModel<ITreeItem> multipleSelection)
        {
            this.SelectedItem = null;
            return;
        }

        this.SelectedItem = multipleSelection.SelectedIndices.Count == 1
            ? this.GetShownItemAt(multipleSelection.SelectedIndices[0])
            : null;
    }

    private async Task<ITreeItem?> FindAdapterByVirtualPathAsync(AssetPath virtualPath)
    {
        if (this.projectRoot == null)
        {
            return null;
        }

        var mountName = virtualPath.Mount;
        var children = await this.projectRoot.Children.ConfigureAwait(true);

        // A folder of the project outside its other mounts is under the project's own files mount, shown as the project's
        // row; that row itself is no folder: it stands for the whole project.
        TreeItemAdapter? mount = !virtualPath.IsMount && string.Equals(mountName, ProjectAssetFiles.ProjectFilesMount, StringComparison.Ordinal)
            ? this.projectRoot
            : null;
        mount ??= children.OfType<AuthoringMountPointTreeItemAdapter>()
            .FirstOrDefault(m => string.Equals(m.MountPoint.Name, mountName, StringComparison.Ordinal));
        mount ??= children.OfType<VirtualFolderMountTreeItemAdapter>()
            .FirstOrDefault(m => string.Equals(m.MountPointName, mountName, StringComparison.Ordinal));
        mount ??= children.OfType<PakMountTreeItemAdapter>()
            .FirstOrDefault(m => string.Equals(m.MountPointName, mountName, StringComparison.Ordinal));

        if (mount == null)
        {
            return null;
        }

        return virtualPath.IsMount ? mount : await FindFolderAdapterAsync(mount, virtualPath.RelativePath).ConfigureAwait(true);
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
                case PakMountTreeItemAdapter pak:
                    _ = result.Add(pak.MountPointName);
                    break;
            }
        }

        return result;
    }

    private AssetPath? GetVirtualPath(ITreeItem item)
        => item switch
        {
            AuthoringMountPointTreeItemAdapter authoring => authoring.VirtualRootPath,
            VirtualFolderMountTreeItemAdapter virtualMount => virtualMount.VirtualRootPath,
            PakMountTreeItemAdapter pak => pak.VirtualRootPath,
            FolderTreeItemAdapter folder => this.GetFolderVirtualPath(folder),
            CookedFolderTreeItemAdapter folder => folder.VirtualPath,
            _ => null,
        };

    // A folder under a mount is named from the mount; a folder of the project outside the mounts, from the catalog of
    // the project's own files.
    private AssetPath? GetFolderVirtualPath(FolderTreeItemAdapter folder)
    {
        var location = FileSystemPath.From(folder.Folder.Location);
        for (var current = folder.Parent; current is not null; current = current.Parent)
        {
            (string? root, AssetPath? mount) = current switch
            {
                AuthoringMountPointTreeItemAdapter authoring => (authoring.RootFolder.Location, authoring.VirtualRootPath),
                VirtualFolderMountTreeItemAdapter mounted => (mounted.RootFolder.Location, mounted.VirtualRootPath),
                ProjectRootTreeItemAdapter project => (project.ProjectRootFolder.Location, AssetCatalog.ProjectFiles),
                _ => (null, null),
            };
            if (root is not null && mount is not null)
            {
                var mountRoot = FileSystemPath.From(root);
                return location.IsSameOrInside(mountRoot) && mount.TryCombine(location.GetPathBelow(mountRoot), out var path) ? path : null;
            }
        }

        return null;
    }

    private void OnMountPointPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(ITreeItem.IsExpanded), StringComparison.Ordinal))
        {
            this.HasUnsavedChanges = true;
        }
    }
}
