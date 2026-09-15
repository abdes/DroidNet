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
using DroidNet.Storage;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Filesystem;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>
///     Represents the ViewModel for the project layout in the content browser.
///     Acts as a mediator between the DynamicTree control and ContentBrowserState.
/// </summary>
/// <param name="projectContextService">The active project context service.</param>
/// <param name="storage">The storage provider.</param>
/// <param name="contentBrowserState">The state of the content browser.</param>
/// <param name="loggerFactory">
///     Optional factory for creating loggers. If provided, enables detailed logging of the recognition
///     process. If <see langword="null" />, logging is disabled.
/// </param>
public partial class ProjectLayoutViewModel(
    IProjectContextService projectContextService,
    IStorageProvider storage,
    ContentBrowserState contentBrowserState,
    IDialogService dialogService,
    ViewModelToView vmToView,
    IMessenger messenger,
    ILoggerFactory? loggerFactory)
    : DynamicTreeViewModel(loggerFactory), IRoutingAware
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<ProjectLayoutViewModel>() ??
                                      NullLoggerFactory.Instance.CreateLogger<ProjectLayoutViewModel>();

    private readonly ViewModelToView vmToView = vmToView;
    private readonly IMessenger messenger = messenger;

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

    /// <summary>
    ///     Gets a value indicating whether the current selection can be unmounted.
    /// </summary>
    public bool CanUnmountSelectedItem
    {
        get => !this.IsApplyingMounts && this.canUnmountSelectedItem;
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
        get => !this.IsApplyingMounts && this.canRenameSelectedItem;
        private set
        {
            if (this.SetProperty(ref this.canRenameSelectedItem, value))
            {
                this.RenameSelectedItemCommand.NotifyCanExecuteChanged();
            }
        }
    }

    /// <inheritdoc />
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The UI operation boundary reports failures while keeping the browser and other mounted content usable.")]
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
                this.LogFolderNavigationFailure(ex);
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
        try
        {
            var context = projectContextService.ActiveProject;
            var projectInfo = this.GetActiveProjectInfo()
                ?? throw new InvalidOperationException("Project Layout used with no CurrentProject");
            var folder = await storage.GetFolderFromPathAsync(projectInfo.Location!).ConfigureAwait(true);
            if (this.selectionDisposed || !ReferenceEquals(context, projectContextService.ActiveProject))
            {
                return;
            }

            this.suppressTreeSelectionEvents = true;
            try
            {
                if (this.projectRoot is not null)
                {
                    this.projectRoot.MountRenamed -= this.OnMountRenamed;
                    this.projectRoot.Dispose();
                }

                this.projectRoot = new ProjectRootTreeItemAdapter(this.logger, storage, projectInfo, folder) { IsExpanded = true };
                await this.LoadPersistedMountsAsync(storage, projectInfo).ConfigureAwait(true);
                if (this.selectionDisposed || !ReferenceEquals(context, projectContextService.ActiveProject))
                {
                    return;
                }

                this.projectRoot.MountRenamed += this.OnMountRenamed;
                _ = this.projectRoot.Children;
                await this.InitializeRootAsync(this.projectRoot, skipRoot: false).ConfigureAwait(true);
            }
            finally
            {
                this.suppressTreeSelectionEvents = false;
            }

            await this.UpdateTreeSelectionFromStateAsync().ConfigureAwait(true);
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
        ArgumentNullException.ThrowIfNull(folder);
        var root = this.projectRoot;
        var version = ++this.selectionRequestVersion;
        if (root is null || this.selectionDisposed)
        {
            return;
        }

        var path = await this.ResolveFolderNavigationPathAsync(root, folder).ConfigureAwait(true);
        if (path is not null && this.IsSelectionRequestCurrent(version, root))
        {
            contentBrowserState.SetSelectedFolders([path]);
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

    [RelayCommand(CanExecute = nameof(CanChangeMounts))]
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The UI operation boundary reports failures while keeping the browser and other mounted content usable.")]
    private async Task MountKnownLocationAsync(KnownVirtualFolderMount kind)
    {
        if (projectContextService.ActiveProject is not { } expected || this.GetActiveProjectInfo() is not { } candidate)
        {
            return;
        }

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
            contentBrowserState.SetSelectedFolders(["/" + existing.Name]);
            return;
        }

        candidate.AuthoringMounts.Add(new(name, path));
        await this.ApplyMountCandidateAsync(expected, candidate).ConfigureAwait(true);
        if (projectContextService.ActiveProject?.AuthoringMounts.Any(mount => string.Equals(mount.Name, name, StringComparison.Ordinal) && string.Equals(mount.RelativePath, path, StringComparison.Ordinal)) == true)
        {
            contentBrowserState.SetSelectedFolders(["/" + name]);
        }
    }

    [RelayCommand(CanExecute = nameof(CanChangeMounts))]
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The UI operation boundary reports failures while keeping the browser and other mounted content usable.")]
    private async Task MountLocalFolderAsync()
    {
        if (projectContextService.ActiveProject is not { } expected || this.GetActiveProjectInfo() is not { } candidate)
        {
            return;
        }

        var names = await this.GetExistingMountPointNamesAsync().ConfigureAwait(true);
        var model = new LocalFolderMountDialogViewModel(dialogService, names);
        if (!await this.ShowLocalMountDialogAsync(model).ConfigureAwait(true) || model.Result is not { } definition)
        {
            return;
        }

        var relative = GetRelativeMountPath(expected.ProjectRoot, definition.AbsoluteFolderPath);
        var isCooked = await storage.DocumentExistsAsync(Path.Combine(definition.AbsoluteFolderPath, "container.index.bin")).ConfigureAwait(true);
        if (relative is not null && !isCooked)
        {
            candidate.AuthoringMounts.Add(new(definition.MountPointName, relative));
        }
        else
        {
            candidate.LocalFolderMounts.Add(new(definition.MountPointName, definition.AbsoluteFolderPath));
        }

        await this.ApplyMountCandidateAsync(expected, candidate).ConfigureAwait(true);
    }

    [RelayCommand(CanExecute = nameof(CanUnmountSelectedItem))]
    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The UI operation boundary reports failures while keeping the browser and other mounted content usable.")]
    private async Task UnmountSelectedItemAsync()
    {
        if (projectContextService.ActiveProject is not { } expected || this.GetActiveProjectInfo() is not { } candidate)
        {
            return;
        }

        var name = this.SelectedItem switch
        {
            VirtualFolderMountTreeItemAdapter mount => mount.MountPointName,
            AuthoringMountPointTreeItemAdapter mount => mount.MountPoint.Name,
            _ => null,
        };
        if (name is null)
        {
            return;
        }

        candidate.AuthoringMounts = candidate.AuthoringMounts.Where(mount => !string.Equals(mount.Name, name, StringComparison.Ordinal)).ToList();
        candidate.LocalFolderMounts = candidate.LocalFolderMounts.Where(mount => !string.Equals(mount.Name, name, StringComparison.Ordinal)).ToList();
        candidate.CookedContentOrder = candidate.CookedContentOrder.Where(source => source.Kind != CookedContentSourceKind.LocalFolder || !string.Equals(source.Name, name, StringComparison.Ordinal)).ToList();
        await this.ApplyMountCandidateAsync(expected, candidate).ConfigureAwait(true);
    }

    [RelayCommand(CanExecute = nameof(CanRenameSelectedItem))]
    private void RenameSelectedItem()
    {
        if (this.SelectedItem is null)
        {
            return;
        }

        // Rename is performed via the DynamicTree in-place rename UI.
        this.RenameRequested?.Invoke(this, new(this.SelectedItem));
    }

    private void RestoreState()
    {
        Debug.Assert(this.activeRoute is not null, "should have an active route");

        this.LogRestoreStateStart();

        var selectedFolders = RouteStateMapping.GetSelectedFolders(this.activeRoute);
        foreach (var relativePath in selectedFolders)
        {
            this.LogRestoreStateAddFolder(relativePath);
        }

        // Important: apply selection via ContentBrowserState so that PropertyChanged is raised.
        // The assets pane refreshes itself on SelectedFolders PropertyChanged.
        contentBrowserState.SetSelectedFolders(selectedFolders);

        this.LogRestoreStateFinal(string.Join(", ", contentBrowserState.SelectedFolders));
    }

    private void OnMountRenamed(object? sender, VirtualFolderMountTreeItemAdapter mount)
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
        var activeProject = projectContextService.ActiveProject;
        var projectInfo = this.GetActiveProjectInfo();
        if (activeProject is null || projectInfo is null || this.projectRoot is null)
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

    private async Task LoadPersistedMountsAsync(IStorageProvider storage, ProjectInfo projectInfo)
    {
        if (this.projectRoot is null)
        {
            return;
        }

        foreach (var mount in projectInfo.AuthoringMounts.Where(IsPersistedProjectRelativeVirtualMount))
        {
            await this.RestoreMountAsync(storage, mount.Name, mount.RelativePath, isProjectRelative: true, mount.IsExpanded).ConfigureAwait(true);
        }

        foreach (var mount in projectInfo.LocalFolderMounts)
        {
            await this.RestoreMountAsync(storage, mount.Name, mount.AbsolutePath, isProjectRelative: false, mount.IsExpanded).ConfigureAwait(true);
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
                var projectInfo = this.GetActiveProjectInfo() ??
                                  throw new InvalidOperationException("Project Layout used with no CurrentProject");

                // Create the root TreeItem for the project root folder.
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

    private ProjectInfo? GetActiveProjectInfo()
    {
        var context = projectContextService.ActiveProject;

        if (context is null)
        {
            this.LogNoCurrentProject();
            return null;
        }

        var projectInfo = new ProjectInfo(
            context.ProjectId,
            context.Name,
            context.Category,
            context.ProjectRoot,
            context.Thumbnail);
        foreach (var mount in context.AuthoringMounts)
        {
            projectInfo.AuthoringMounts.Add(mount);
        }

        foreach (var mount in context.LocalFolderMounts)
        {
            projectInfo.LocalFolderMounts.Add(mount);
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
