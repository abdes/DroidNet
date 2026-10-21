// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>
/// A pak mounted read-only: the engine content pak, or a pak the project declares. Its folders are the virtual folders
/// of the assets it lists; selecting one shows those assets.
/// </summary>
public sealed class PakMountTreeItemAdapter : TreeItemAdapter, ILogicalFolderRoot
{
    private readonly IReadOnlyList<CookedFolderTreeItemAdapter> folders;

    /// <summary>Initializes a new instance of the <see cref="PakMountTreeItemAdapter"/> class.</summary>
    /// <param name="mountPointName">The mount name the tree shows and the browser selects the pak by.</param>
    /// <param name="pak">The pak file.</param>
    /// <param name="assetPaths">The pak's asset paths relative to the mount, such as <c>Generated/BasicShapes/Cube.ogeo</c>; their folders become the mount's folders.</param>
    public PakMountTreeItemAdapter(string mountPointName, FileSystemPath pak, IEnumerable<string> assetPaths)
        : base(isRoot: false, isHidden: false)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(mountPointName);
        ArgumentNullException.ThrowIfNull(pak);
        ArgumentNullException.ThrowIfNull(assetPaths);
        this.MountPointName = mountPointName;
        this.Pak = pak;
        this.folders = CookedFolderTreeItemAdapter.Build(this, assetPaths);
    }

    /// <inheritdoc />
    public override string Label
    {
        get => this.MountPointName;
        set => throw new InvalidOperationException("A pak mount is renamed in the Content mounts dialog.");
    }

    /// <summary>Gets the mount name.</summary>
    public string MountPointName { get; }

    /// <summary>Gets the pak file.</summary>
    public FileSystemPath Pak { get; }

    /// <inheritdoc />
    public AssetPath VirtualRootPath => AssetPath.Create(this.MountPointName);

    /// <summary>Gets the package glyph pak mounts show.</summary>
    public string IconGlyph => "";

    /// <inheritdoc />
    public override bool ValidateItemName(string name) => false;

    /// <inheritdoc />
    protected override int DoGetChildrenCount() => this.folders.Count;

    /// <inheritdoc />
    protected override Task LoadChildren()
    {
        foreach (var folder in this.folders)
        {
            this.AddChildInternal(folder);
        }

        return Task.CompletedTask;
    }
}
