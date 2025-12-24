// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Controls;
using Microsoft.Extensions.Logging;
using Oxygen.Assets.Filesystem;
using Oxygen.Core;
using Oxygen.Editor.World;
using Oxygen.Storage;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>
///     Represents an authoring mount point node under the project root.
/// </summary>
/// <remarks>
///     Authoring mount points are persisted in <c>Project.oxy</c> via <see cref="IProjectInfo.AuthoringMounts" />
///     and define stable virtual roots for asset identity.
/// </remarks>
public sealed partial class AuthoringMountPointTreeItemAdapter : TreeItemAdapter, IDisposable
{
    private readonly ILogger logger;
    private bool disposed;

    private string label;

    /// <summary>
    ///     Initializes a new instance of the <see cref="AuthoringMountPointTreeItemAdapter"/> class.
    /// </summary>
    /// <param name="logger">Logger used to record load failures.</param>
    /// <param name="mountPoint">The persisted authoring mount point definition.</param>
    /// <param name="rootFolder">The backing folder for the mount point.</param>
    public AuthoringMountPointTreeItemAdapter(ILogger logger, ProjectMountPoint mountPoint, IFolder rootFolder)
        : base(isRoot: false, isHidden: false)
    {
        this.logger = logger;
        this.MountPoint = mountPoint;
        this.RootFolder = rootFolder;
        this.label = mountPoint.Name;

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

            this.label = value;
            this.OnPropertyChanged();
        }
    }

    /// <summary>
    ///     Gets the persisted mount point definition.
    /// </summary>
    public ProjectMountPoint MountPoint { get; }

    /// <summary>
    ///     Gets the canonical absolute virtual root path for this authoring mount point.
    /// </summary>
    /// <remarks>
    ///     This is the stable identity for asset selection and must be case-sensitive.
    /// </remarks>
    public string VirtualRootPath => VirtualPath.CreateAbsolute(this.MountPoint.Name);

    /// <summary>
    ///     Gets the backing root folder for this mount.
    /// </summary>
    public IFolder RootFolder { get; }

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
        => other is AuthoringMountPointTreeItemAdapter otherMount
            && string.Equals(this.MountPoint.Name, otherMount.MountPoint.Name, StringComparison.Ordinal)
            && string.Equals(this.MountPoint.RelativePath, otherMount.MountPoint.RelativePath, StringComparison.Ordinal);

    /// <inheritdoc />
    public override int GetHashCode() => HashCode.Combine(
        StringComparer.Ordinal.GetHashCode(this.MountPoint.Name),
        StringComparer.Ordinal.GetHashCode(this.MountPoint.RelativePath));

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
