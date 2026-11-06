// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>
/// A declared mount this machine cannot read: a placeholder no <c>.mounts.json</c> binds, or a mount whose folder or pak
/// file is missing. It stays in the tree, dimmed, with its state beside its name and no children, and its context menu
/// binds or overrides it.
/// </summary>
public sealed class UnavailableMountTreeItemAdapter : TreeItemAdapter
{
    /// <summary>Initializes a new instance of the <see cref="UnavailableMountTreeItemAdapter"/> class.</summary>
    /// <param name="mount">The unavailable mount.</param>
    /// <param name="projectRoot">The project root its committed path is relative to.</param>
    public UnavailableMountTreeItemAdapter(ResolvedMount mount, FileSystemPath projectRoot)
        : base(isRoot: false, isHidden: false)
    {
        ArgumentNullException.ThrowIfNull(mount);
        ArgumentNullException.ThrowIfNull(projectRoot);
        this.Mount = mount;
        this.IsDimmed = true;
        this.Annotation = mount.State switch { MountState.Unbound => "Not bound", MountState.Invalid => "Cannot be used", _ => "Folder missing" };
        this.StateToolTip = mount.State == MountState.Invalid
            ? string.Join('\n', mount.Findings.Select(static finding => finding.Message))
            : mount.State == MountState.Unbound
            ? $"{mount.Name} is declared without a folder. Bind it to a folder on this machine; the binding is saved in .mounts.json, which is not committed."
            : $"{mount.Name}: the folder is missing.\n{(mount.Origin == MountOrigin.MountsFile ? $"{mount.OriginFile?.Name}: {mount.Location}" : $"Project.oxy: {mount.DeclaredPath}")}\nLooked for: {mount.Location}. Override it on this machine.";
    }

    /// <inheritdoc />
    public override string Label
    {
        get => this.Mount.Name;
        set => throw new InvalidOperationException("A mount is renamed in the Content mounts dialog.");
    }

    /// <summary>Gets the unavailable mount.</summary>
    public ResolvedMount Mount { get; }

    /// <summary>Gets the glyph of the mount's kind: a package for a pak file, a drive for a folder.</summary>
    public string IconGlyph => this.Mount.IsPakFile ? "" : "";

    /// <summary>Gets what the state's tooltip says: why the mount cannot be read, and what to do.</summary>
    public string StateToolTip { get; }

    /// <inheritdoc />
    public override bool ValidateItemName(string name) => false;

    /// <inheritdoc />
    public override bool Equals(ITreeItem? other)
        => other is UnavailableMountTreeItemAdapter mount && string.Equals(mount.Mount.Name, this.Mount.Name, StringComparison.Ordinal);

    /// <inheritdoc />
    public override int GetHashCode() => StringComparer.Ordinal.GetHashCode(this.Mount.Name);

    /// <inheritdoc />
    protected override int DoGetChildrenCount() => 0;

    /// <inheritdoc />
    protected override Task LoadChildren() => Task.CompletedTask;
}
