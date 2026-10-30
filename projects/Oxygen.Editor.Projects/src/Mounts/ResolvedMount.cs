// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects.Mounts;

/// <summary>
/// A mount of the resolved mount table: a declared or locally added mount with its location on this machine and
/// where that came from. Locations are local facts: they are never stored in committed files.
/// </summary>
/// <param name="Name">The mount name.</param>
/// <param name="Kind">What the mount holds.</param>
/// <param name="Scope">Whether <c>Project.oxy</c> declares the mount or only a <c>.mounts.json</c> adds it.</param>
/// <param name="DeclaredPath">The committed path, or <see langword="null"/> for a placeholder or a local mount.</param>
/// <param name="Location">The canonical folder, or pak file, or <see langword="null"/> when unbound.</param>
/// <param name="Origin">Which file gave the location.</param>
/// <param name="OriginFile">The <c>.mounts.json</c> that gave the location, if one did.</param>
/// <param name="State">Whether the location exists.</param>
/// <param name="IsExpanded">Whether the Content Explorer shows the mount expanded.</param>
public sealed record ResolvedMount(
    string Name,
    MountKind Kind,
    MountScope Scope,
    string? DeclaredPath,
    FileSystemPath? Location,
    MountOrigin Origin,
    FileSystemPath? OriginFile,
    MountState State,
    bool IsExpanded = true)
{
    /// <summary>Gets a value indicating whether the mount has a location that exists.</summary>
    public bool IsBound => this.State == MountState.Bound;

    /// <summary>Gets the declaration this mount commits, or <see langword="null"/> for a local mount.</summary>
    public ProjectMount? Declaration
        => this.Scope == MountScope.Declared ? new(this.Name, this.Kind, this.DeclaredPath, this.IsExpanded) : null;
}
