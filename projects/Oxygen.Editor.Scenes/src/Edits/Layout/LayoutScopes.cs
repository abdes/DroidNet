// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Layout;

/// <summary>The scene scope of explorer layout places: the node whose children a place groups, or the root.</summary>
/// <remarks>
/// A node's entry lives in the scope of its scene parent. Moving an entry within its scope is a layout edit; moving it
/// into another scope also reparents the node, so callers build a batch.
/// </remarks>
public static class LayoutScopes
{
    /// <summary>Gets the scope of a layout place.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="position">The place.</param>
    /// <param name="scope">The node whose children the place groups, or <see langword="null"/> for the root.</param>
    /// <returns>Whether the place's container is in the layout.</returns>
    public static bool TryScopeOf(Scene scene, LayoutPosition position, out NodeId? scope)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(position);
        (var found, scope) = SceneLayout.ScopeOf(scene.ExplorerLayout ?? [], position);
        return found || (position.Folder is null && position.Node is null);
    }

    /// <summary>Gets the scope a layout entry lives in.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="entry">The entry.</param>
    /// <param name="scope">The node whose children the entry groups, or <see langword="null"/> for the root.</param>
    /// <returns>Whether the entry is in the layout.</returns>
    public static bool TryScopeOf(Scene scene, LayoutEntryId entry, out NodeId? scope)
    {
        ArgumentNullException.ThrowIfNull(scene);
        (var found, scope) = SceneLayout.ScopeOfEntry(scene.ExplorerLayout ?? [], entry);
        return found;
    }

    /// <summary>Gets where a layout entry is.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="entry">The entry.</param>
    /// <returns>Its place, or <see langword="null"/> when the layout has no such entry.</returns>
    public static LayoutPosition? PositionOf(Scene scene, LayoutEntryId entry)
    {
        ArgumentNullException.ThrowIfNull(scene);
        return SceneLayout.PositionOf(scene.ExplorerLayout ?? [], entry);
    }

    /// <summary>Gets the entries of a container, in order.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="container">Any place in the container; its index is ignored.</param>
    /// <returns>The entries; empty when the layout has no such container.</returns>
    public static IReadOnlyList<LayoutEntryId> EntriesOf(Scene scene, LayoutPosition container)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(container);
        return [.. (SceneLayout.ContainerOf(scene.ExplorerLayout ?? [], container) ?? []).Select(SceneLayout.IdOf)];
    }

    /// <summary>Gets the place after the last entry of a container, as a move of some of its entries counts it.</summary>
    /// <remarks>A move takes its index after the moved entries left their places, so entries that move out do not count.</remarks>
    /// <param name="scene">The scene.</param>
    /// <param name="container">Any place in the container; its index is ignored.</param>
    /// <param name="moving">The entries about to move, if any.</param>
    /// <returns>The place at the container's end.</returns>
    public static LayoutPosition EndOf(Scene scene, LayoutPosition container, IEnumerable<LayoutEntryId>? moving = null)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(container);
        var leaving = moving?.ToHashSet() ?? [];
        return container with { Index = EntriesOf(scene, container).Count(entry => !leaving.Contains(entry)) };
    }
}
