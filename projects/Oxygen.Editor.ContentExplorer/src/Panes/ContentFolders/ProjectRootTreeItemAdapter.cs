// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Controls;
using DroidNet.Storage;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>
///     Represents the Project Explorer root node.
/// </summary>
/// <remarks>
///     Children include:
///     - Non-hidden folders directly under the project root (excluding dot-prefixed folders)
///     - Authoring mount points (from <see cref="IProjectInfo.AuthoringMounts" />)
///     - Explicitly mounted virtual folders.
/// </remarks>
public sealed partial class ProjectRootTreeItemAdapter : TreeItemAdapter, IDisposable
{
    private readonly ILogger logger;
    private readonly IStorageProvider storage;
    private readonly Dictionary<string, VirtualFolderMountTreeItemAdapter> virtualFolderMounts;
    private readonly List<PakMountTreeItemAdapter> pakMounts = [];

    private bool disposed;

    private string label;

    /// <summary>
    ///     Initializes a new instance of the <see cref="ProjectRootTreeItemAdapter"/> class.
    /// </summary>
    /// <param name="logger">Logger used to record load failures.</param>
    /// <param name="storage">Storage provider used to resolve project-relative paths.</param>
    /// <param name="projectInfo">The current project info.</param>
    /// <param name="projectRootFolder">The project root folder handle.</param>
    public ProjectRootTreeItemAdapter(
        ILogger logger,
        IStorageProvider storage,
        IProjectInfo projectInfo,
        IFolder projectRootFolder)
        : base(isRoot: true, isHidden: false)
    {
        this.logger = logger;
        this.storage = storage;
        this.ProjectInfo = projectInfo;
        this.ProjectRootFolder = projectRootFolder;
        this.label = projectInfo.Name;

        this.virtualFolderMounts = new Dictionary<string, VirtualFolderMountTreeItemAdapter>(StringComparer.Ordinal);

        this.ChildrenCollectionChanged += (_, _) => this.OnPropertyChanged(nameof(this.IconGlyph));
    }

    /// <summary>
    ///     Raised when a virtual folder mount has been renamed.
    /// </summary>
    public event EventHandler<MountRenamedEventArgs>? MountRenamed;

    /// <inheritdoc />
    public override string Label
    {
        get => this.label;
        set
        {
            if (string.Equals(value, this.label, StringComparison.Ordinal))
            {
                return;
            }

            this.label = value;
            this.OnPropertyChanged();
        }
    }

    /// <inheritdoc />
    /// <remarks>The tree root shows the project's name, as the breadcrumb does.</remarks>
    public override string DisplayLabel => this.label;

    /// <summary>
    ///     Gets the current project info.
    /// </summary>
    public IProjectInfo ProjectInfo { get; }

    /// <summary>
    ///     Gets the project root folder.
    /// </summary>
    public IFolder ProjectRootFolder { get; }

    /// <summary>
    ///     Gets the icon glyph for the folder.
    /// </summary>
    public string IconGlyph => this.IsExpanded && this.ChildrenCount > 0 ? "\uE838" : "\uE8B7";

    /// <summary>
    ///     Gets the collection of currently mounted virtual folders.
    /// </summary>
    public IReadOnlyCollection<VirtualFolderMountTreeItemAdapter> VirtualFolderMounts => this.virtualFolderMounts.Values;

    /// <inheritdoc />
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <inheritdoc />
    public override bool ValidateItemName(string name)
    {
        if (VirtualPath.ValidateMountName(name, VirtualPathRuleSet.Authored) is not null)
        {
            return false;
        }

        // A name equal to another mount's, or differing from it only in case, is refused: on a case-insensitive file
        // system the two would share their cooked folder.
        return !this.virtualFolderMounts.Keys.Concat(this.pakMounts.Select(static pak => pak.MountPointName))
            .Any(existing => VirtualPathComparer.CaseFolded.Equals(existing, name));
    }

    /// <summary>
    ///     Adds a virtual folder mount as a direct child of the project root.
    /// </summary>
    /// <param name="mount">The mount adapter to add.</param>
    /// <returns><see langword="true"/> when added; otherwise <see langword="false"/> when a mount with the same name already exists.</returns>
    public async Task<bool> MountVirtualFolderAsync(VirtualFolderMountTreeItemAdapter mount)
    {
        ArgumentNullException.ThrowIfNull(mount);

        if (this.virtualFolderMounts.ContainsKey(mount.MountPointName))
        {
            return false;
        }

        this.virtualFolderMounts.Add(mount.MountPointName, mount);
        return true;
    }

    /// <summary>Adds a pak mount after the folder mounts; the engine content comes first, then the declared paks.</summary>
    /// <param name="pak">The pak mount to add.</param>
    /// <returns><see langword="true"/> when added; otherwise <see langword="false"/> when a mount with the same name already exists.</returns>
    public bool MountPak(PakMountTreeItemAdapter pak)
    {
        ArgumentNullException.ThrowIfNull(pak);
        if (this.virtualFolderMounts.ContainsKey(pak.MountPointName)
            || this.pakMounts.Exists(existing => string.Equals(existing.MountPointName, pak.MountPointName, StringComparison.Ordinal)))
        {
            return false;
        }

        this.pakMounts.Add(pak);
        return true;
    }

    /// <summary>
    ///     Removes a previously mounted virtual folder.
    /// </summary>
    /// <param name="mountPointName">The mount point identity name (case-sensitive).</param>
    /// <returns><see langword="true"/> when removed; otherwise <see langword="false"/> if not mounted.</returns>
    public async Task<bool> UnmountVirtualFolderAsync(string mountPointName)
    {
        ArgumentException.ThrowIfNullOrEmpty(mountPointName);

        _ = await this.Children.ConfigureAwait(false);

        if (!this.virtualFolderMounts.TryGetValue(mountPointName, out var mount))
        {
            return false;
        }

        _ = this.virtualFolderMounts.Remove(mountPointName);
        mount.Dispose();
        return true;
    }

    /// <summary>
    ///     Notifies the root that a virtual folder mount has been renamed.
    /// </summary>
    /// <param name="mount">The mount that was renamed.</param>
    /// <param name="oldName">The previous name of the mount.</param>
    internal void NotifyMountRenamed(VirtualFolderMountTreeItemAdapter mount, string oldName)
    {
        ArgumentNullException.ThrowIfNull(mount);

        if (this.virtualFolderMounts.Remove(oldName))
        {
            this.virtualFolderMounts.Add(mount.MountPointName, mount);
            this.MountRenamed?.Invoke(this, new MountRenamedEventArgs(mount));
        }
    }

    /// <inheritdoc />
    protected override int DoGetChildrenCount() => 1;

    /// <inheritdoc />
    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "Tree load should be resilient to partial failures")]
    protected override async Task LoadChildren()
    {
        try
        {
            await this.LoadProjectFoldersAsync().ConfigureAwait(true);
            await this.LoadAuthoringMountsAsync().ConfigureAwait(true);
            foreach (var mount in this.virtualFolderMounts.Values)
            {
                this.AddChildInternal(mount);
            }

            foreach (var pak in this.pakMounts)
            {
                this.AddChildInternal(pak);
            }
        }
        catch (Exception ex)
        {
            this.CouldNotLoadProjectFolders(this.ProjectRootFolder.Location, ex);
        }
    }

    /// <inheritdoc />
    protected override void OnPropertyChanged(PropertyChangedEventArgs e)
    {
        base.OnPropertyChanged(e);

        if (string.Equals(e.PropertyName, nameof(this.IsExpanded), StringComparison.Ordinal))
        {
            this.OnPropertyChanged(nameof(this.IconGlyph));
        }
    }

    private static bool IsDerivedRootMount(ProjectMountPoint mount)
    {
        var relativePath = mount.RelativePath.Trim().Replace('\\', '/').Trim('/');
        return string.Equals(relativePath, ".cooked", StringComparison.OrdinalIgnoreCase)
               || string.Equals(relativePath, ".imported", StringComparison.OrdinalIgnoreCase)
               || string.Equals(relativePath, ".build", StringComparison.OrdinalIgnoreCase);
    }

    private HashSet<string> GetExcludedDirectFolderNames()
    {
        var excluded = new HashSet<string>(StringComparer.Ordinal);
        foreach (var mount in this.ProjectInfo.AuthoringMounts)
        {
            if (IsDerivedRootMount(mount))
            {
                continue;
            }

            // Only exclude direct children of the project root.
            var relative = mount.RelativePath.Trim().Replace('\\', '/');
            if (relative.Contains('/', StringComparison.Ordinal))
            {
                continue;
            }

            if (!string.IsNullOrEmpty(relative))
            {
                _ = excluded.Add(relative);
            }
        }

        return excluded;
    }

    // Non-hidden folders directly under the project root, except those mounts and outputs already show.
    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "Tree load should be resilient to partial failures")]
    private async Task LoadProjectFoldersAsync()
    {
        var excludedDirectFolderNames = this.GetExcludedDirectFolderNames();
        await foreach (var child in this.ProjectRootFolder.GetFoldersAsync().ConfigureAwait(true))
        {
            if (child.Name.StartsWith('.') || excludedDirectFolderNames.Contains(child.Name))
            {
                continue;
            }

            FolderTreeItemAdapter? item = null;
            try
            {
                item = new FolderTreeItemAdapter(this.logger, child, child.Name);
                this.AddChildInternal(item);
                item = null;
            }
            catch (Exception ex)
            {
                this.CouldNotLoadProjectFolders(this.ProjectRootFolder.Location, ex);
            }
            finally
            {
                item?.Dispose();
            }
        }
    }

    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "Tree load should be resilient to partial failures")]
    private async Task LoadAuthoringMountsAsync()
    {
        foreach (var mountPoint in this.ProjectInfo.AuthoringMounts.Where(static mount => !IsDerivedRootMount(mount)))
        {
            AuthoringMountPointTreeItemAdapter? mountAdapter = null;
            try
            {
                var mountRootLocation = this.storage.NormalizeRelativeTo(this.ProjectRootFolder.Location, mountPoint.RelativePath);
                var mountRootFolder = await this.storage.GetFolderFromPathAsync(mountRootLocation).ConfigureAwait(true);
                mountAdapter = new AuthoringMountPointTreeItemAdapter(this.logger, mountPoint, mountRootFolder)
                {
                    IsExpanded = mountPoint.IsExpanded,
                };
                this.AddChildInternal(mountAdapter);
                mountAdapter = null;
            }
            catch (Exception ex)
            {
                this.CouldNotLoadProjectFolders(this.ProjectRootFolder.Location, ex);
            }
            finally
            {
                mountAdapter?.Dispose();
            }
        }
    }

    private void Dispose(bool disposing)
    {
        if (!this.disposed && disposing)
        {
            foreach (var mount in this.virtualFolderMounts.Values)
            {
                mount.Dispose();
            }

            this.virtualFolderMounts.Clear();
            this.disposed = true;
        }
    }
}
