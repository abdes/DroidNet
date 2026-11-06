// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>
/// A library mounted read-only: the engine content pak, or a pak or loose cooked content the project declares. Its folders
/// are the virtual folders of the assets it holds; selecting one shows those assets.
/// </summary>
public sealed class LibraryMountTreeItemAdapter : TreeItemAdapter, ILogicalFolderRoot
{
    private readonly IReadOnlyList<CookedFolderTreeItemAdapter> folders;

    /// <summary>Initializes a new instance of the <see cref="LibraryMountTreeItemAdapter"/> class.</summary>
    /// <param name="mountPointName">The mount name the tree shows and the browser selects the library by.</param>
    /// <param name="location">What is read.</param>
    /// <param name="isPak">Whether it is a pak file.</param>
    /// <param name="assetPaths">The library's asset paths relative to the mount, such as <c>Generated/BasicShapes/Cube.ogeo</c>; their folders become the mount's folders.</param>
    public LibraryMountTreeItemAdapter(string mountPointName, FileSystemPath location, bool isPak, IEnumerable<string> assetPaths)
        : base(isRoot: false, isHidden: false)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(mountPointName);
        ArgumentNullException.ThrowIfNull(location);
        ArgumentNullException.ThrowIfNull(assetPaths);
        this.MountPointName = mountPointName;
        this.Location = location;
        this.IsPak = isPak;
        this.folders = CookedFolderTreeItemAdapter.Build(this, assetPaths);
    }

    /// <inheritdoc />
    public override string Label
    {
        get => this.MountPointName;
        set => throw new InvalidOperationException("A library is named by what it holds.");
    }

    /// <summary>Gets the mount name.</summary>
    public string MountPointName { get; }

    /// <summary>Gets what is read.</summary>
    public FileSystemPath Location { get; }

    /// <summary>Gets a value indicating whether it is a pak file.</summary>
    public bool IsPak { get; }

    /// <inheritdoc />
    public AssetPath VirtualRootPath => AssetPath.Create(this.MountPointName);

    /// <summary>Gets the package glyph libraries show.</summary>
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
