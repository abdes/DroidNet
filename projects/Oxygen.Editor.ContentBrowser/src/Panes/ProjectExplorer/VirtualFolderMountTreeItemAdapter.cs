// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Controls;
using Microsoft.Extensions.Logging;
using Oxygen.Assets.Filesystem;
using Oxygen.Core;
using Oxygen.Storage;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>
///     Represents a virtual folder mount node (utility/secondary roots like .cooked, .imported, .build, or an arbitrary folder).
/// </summary>
public sealed partial class VirtualFolderMountTreeItemAdapter : TreeItemAdapter, IDisposable
{
    private readonly ILogger logger;
    private bool disposed;

    private string label;

    /// <summary>
    ///     Initializes a new instance of the <see cref="VirtualFolderMountTreeItemAdapter"/> class.
    /// </summary>
    /// <param name="logger">Logger used to record load failures.</param>
    /// <param name="mountPointName">The mount point display/identity name (case-sensitive).</param>
    /// <param name="rootFolder">Backing root folder for this mount.</param>
    public VirtualFolderMountTreeItemAdapter(ILogger logger, string mountPointName, IFolder rootFolder)
        : base(isRoot: false, isHidden: false)
    {
        ArgumentException.ThrowIfNullOrEmpty(mountPointName);

        this.logger = logger;
        this.MountPointName = mountPointName;
        this.RootFolder = rootFolder;
        this.BackingPath = rootFolder.Location;
        this.BackingPathKind = VirtualFolderMountBackingPathKind.Absolute;
        this.label = mountPointName;

        this.ChildrenCollectionChanged += (_, _) => this.OnPropertyChanged(nameof(this.IconGlyph));
    }

    /// <summary>
    ///     Initializes a new instance of the <see cref="VirtualFolderMountTreeItemAdapter"/> class.
    /// </summary>
    /// <param name="logger">Logger used to record load failures.</param>
    /// <param name="mountPointName">The mount point display/identity name (case-sensitive).</param>
    /// <param name="rootFolder">Backing root folder for this mount.</param>
    /// <param name="backingPath">
    ///     The backing folder path string.
    ///     For built-in mounts this should be the project-relative path (e.g. <c>.cooked</c>);
    ///     for Local Folder mounts this should be an absolute path.
    /// </param>
    /// <param name="backingPathKind">Specifies whether <paramref name="backingPath"/> is project-relative or absolute.</param>
    public VirtualFolderMountTreeItemAdapter(
        ILogger logger,
        string mountPointName,
        IFolder rootFolder,
        string backingPath,
        VirtualFolderMountBackingPathKind backingPathKind)
        : base(isRoot: false, isHidden: false)
    {
        ArgumentException.ThrowIfNullOrEmpty(mountPointName);
        ArgumentException.ThrowIfNullOrEmpty(backingPath);

        this.logger = logger;
        this.MountPointName = mountPointName;
        this.RootFolder = rootFolder;
        this.BackingPath = backingPath;
        this.BackingPathKind = backingPathKind;
        this.label = mountPointName;

        this.ChildrenCollectionChanged += (_, _) => this.OnPropertyChanged(nameof(this.IconGlyph));
    }

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

            var oldName = this.MountPointName;
            this.label = value;
            this.MountPointName = value;

            this.OnPropertyChanged();
            this.OnPropertyChanged(nameof(this.MountPointName));
            this.OnPropertyChanged(nameof(this.VirtualRootPath));

            if (this.Parent is ProjectRootTreeItemAdapter root)
            {
                root.NotifyMountRenamed(this, oldName);
            }
        }
    }

    /// <summary>
    ///     Gets the mount point identity name.
    /// </summary>
    public string MountPointName { get; private set; }

    /// <summary>
    ///     Gets the canonical absolute virtual root path for this virtual folder mount.
    /// </summary>
    /// <remarks>
    ///     This is the stable identity for asset selection and must be case-sensitive.
    /// </remarks>
    public string VirtualRootPath => VirtualPath.CreateAbsolute(this.MountPointName);

    /// <summary>
    ///     Gets the backing root folder for this mount.
    /// </summary>
    public IFolder RootFolder { get; }

    /// <summary>
    ///     Gets the backing folder path string used to define this mount.
    /// </summary>
    /// <remarks>
    ///     This is metadata (for display/persistence) and must not be used as identity.
    ///     Identity is <see cref="MountPointName" /> and is case-sensitive.
    /// </remarks>
    public string BackingPath { get; }

    /// <summary>
    ///     Gets whether <see cref="BackingPath"/> is project-relative or absolute.
    /// </summary>
    public VirtualFolderMountBackingPathKind BackingPathKind { get; }

    /// <summary>
    ///     Gets the icon glyph for the mount.
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
    public override bool Equals(ITreeItem? other)
        => other is VirtualFolderMountTreeItemAdapter otherMount
            && string.Equals(this.MountPointName, otherMount.MountPointName, StringComparison.Ordinal)
            && string.Equals(this.BackingPath, otherMount.BackingPath, StringComparison.Ordinal)
            && this.BackingPathKind == otherMount.BackingPathKind;

    /// <inheritdoc />
    public override int GetHashCode() => HashCode.Combine(
        StringComparer.Ordinal.GetHashCode(this.MountPointName),
        StringComparer.Ordinal.GetHashCode(this.BackingPath),
        this.BackingPathKind);

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
            // Virtual folder mounts must tolerate missing backing folders.
            if (!await this.RootFolder.ExistsAsync().ConfigureAwait(true))
            {
                return;
            }

            await foreach (var child in this.RootFolder.GetFoldersAsync().ConfigureAwait(true))
            {
                FolderTreeItemAdapter? item = null;
                try
                {
                    item = new FolderTreeItemAdapter(this.logger, child, child.Name) { IsExpanded = true };
                    this.AddChildInternal(item);
                    item = null;
                }
                catch (Exception ex)
                {
                    this.CouldNotLoadMountFolders(this.RootFolder.Location, ex.Message);
                }
                finally
                {
                    item?.Dispose();
                }
            }
        }
        catch (Exception ex)
        {
            this.CouldNotLoadMountFolders(this.RootFolder.Location, ex.Message);
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

    private void Dispose(bool disposing)
    {
        if (!this.disposed && disposing)
        {
            this.disposed = true;
        }
    }

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "An error occurred while loading folders from mount root `{location}`: {error}")]
    private partial void CouldNotLoadMountFolders(string location, string error);
}
