// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Controls;
using Microsoft.Extensions.Logging;
using Oxygen.Core;
using Oxygen.Editor.World;
using Oxygen.Storage;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>
///     Represents the Project Explorer root node.
/// </summary>
/// <remarks>
///     Children include:
///     - Non-hidden folders directly under the project root (excluding dot-prefixed folders)
///     - Authoring mount points (from <see cref="IProjectInfo.MountPoints" />)
///     - Explicitly mounted virtual folders
/// </remarks>
public sealed partial class ProjectRootTreeItemAdapter : TreeItemAdapter, IDisposable
{
    private readonly ILogger logger;
    private readonly IStorageProvider storage;
    private readonly Dictionary<string, VirtualFolderMountTreeItemAdapter> virtualFolderMounts;

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

    /// <inheritdoc />
    public override string Label
    {
        get => $"{this.label} (Project Root)";
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

    /// <inheritdoc />
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <inheritdoc />
    public override bool ValidateItemName(string name) => InputValidation.IsValidFileName(name);

    /// <inheritdoc />
    protected override int DoGetChildrenCount() => 1;

    /// <summary>
    ///     Adds a virtual folder mount as a direct child of the project root.
    /// </summary>
    /// <param name="mount">The mount adapter to add.</param>
    /// <returns><see langword="true"/> when added; otherwise <see langword="false"/> when a mount with the same name already exists.</returns>
    public async Task<bool> MountVirtualFolderAsync(VirtualFolderMountTreeItemAdapter mount)
    {
        ArgumentNullException.ThrowIfNull(mount);

        _ = await this.Children.ConfigureAwait(false);

        if (this.virtualFolderMounts.ContainsKey(mount.MountPointName))
        {
            return false;
        }

        this.virtualFolderMounts.Add(mount.MountPointName, mount);
        this.AddChildInternal(mount);
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
        this.RemoveChildInternal(mount);
        mount.Dispose();
        return true;
    }

    /// <inheritdoc />
    [SuppressMessage(
        "Design",
        "CA1031:Do not catch general exception types",
        Justification = "Tree load should be resilient to partial failures")]
    protected override async Task LoadChildren()
    {
        try
        {
            var excludedDirectFolderNames = this.GetExcludedDirectFolderNames();

            // 1) Non-hidden folders directly under project root.
            await foreach (var child in this.ProjectRootFolder.GetFoldersAsync().ConfigureAwait(true))
            {
                if (child.Name.StartsWith(".", StringComparison.Ordinal))
                {
                    continue;
                }

                if (excludedDirectFolderNames.Contains(child.Name))
                {
                    continue;
                }

                FolderTreeItemAdapter? item = null;
                try
                {
                    item = new FolderTreeItemAdapter(this.logger, child, child.Name) { IsExpanded = true };
                    this.AddChildInternal(item);
                    item = null;
                }
                catch (Exception ex)
                {
                    this.CouldNotLoadProjectFolders(this.ProjectRootFolder.Location, ex.Message);
                }
                finally
                {
                    item?.Dispose();
                }
            }

            // 2) Authoring mounts.
            foreach (var mountPoint in this.ProjectInfo.MountPoints)
            {
                try
                {
                    var mountRootLocation = this.storage.NormalizeRelativeTo(
                        this.ProjectRootFolder.Location,
                        mountPoint.RelativePath);

                    var mountRootFolder = await this.storage.GetFolderFromPathAsync(mountRootLocation)
                        .ConfigureAwait(true);

                    var mountAdapter = new AuthoringMountPointTreeItemAdapter(
                        this.logger,
                        mountPoint,
                        mountRootFolder)
                    {
                        IsExpanded = true,
                    };

                    this.AddChildInternal(mountAdapter);
                }
                catch (Exception ex)
                {
                    this.CouldNotLoadProjectFolders(this.ProjectRootFolder.Location, ex.Message);
                }
            }

            // 3) Virtual folder mounts are inserted/removed via MountVirtualFolderAsync/UnmountVirtualFolderAsync.
        }
        catch (Exception ex)
        {
            this.CouldNotLoadProjectFolders(this.ProjectRootFolder.Location, ex.Message);
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

    private HashSet<string> GetExcludedDirectFolderNames()
    {
        var excluded = new HashSet<string>(StringComparer.Ordinal);
        foreach (var mount in this.ProjectInfo.MountPoints)
        {
            // Only exclude direct children of the project root.
            var relative = mount.RelativePath.Trim().Replace('\\', '/');
            var firstSlash = relative.IndexOf('/', StringComparison.Ordinal);
            if (firstSlash != -1)
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

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "An error occurred while loading project folders from `{location}`: {error}")]
    private partial void CouldNotLoadProjectFolders(string location, string error);
}
