// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Layout;
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
    /// <param name="layout">The scene's explorer layout.</param>
    /// <param name="position">The place.</param>
    /// <param name="scope">The node whose children the place groups, or <see langword="null"/> for the root.</param>
    /// <returns>Whether the place's container is in the layout.</returns>
    public static bool TryScopeOf(IList<ExplorerEntryData> layout, LayoutPosition position, out NodeId? scope)
    {
        ArgumentNullException.ThrowIfNull(layout);
        ArgumentNullException.ThrowIfNull(position);
        (var found, scope) = SceneLayout.ScopeOf(layout, position);
        return found || (position.Folder is null && position.Node is null);
    }

    /// <summary>Gets the scope a layout entry lives in.</summary>
    /// <param name="layout">The scene's explorer layout.</param>
    /// <param name="entry">The entry.</param>
    /// <param name="scope">The node whose children the entry groups, or <see langword="null"/> for the root.</param>
    /// <returns>Whether the entry is in the layout.</returns>
    public static bool TryScopeOf(IList<ExplorerEntryData> layout, LayoutEntryId entry, out NodeId? scope)
    {
        ArgumentNullException.ThrowIfNull(layout);
        (var found, scope) = SceneLayout.ScopeOfEntry(layout, entry);
        return found;
    }

    /// <summary>Gets where a layout entry is.</summary>
    /// <param name="layout">The scene's explorer layout.</param>
    /// <param name="entry">The entry.</param>
    /// <returns>Its place, or <see langword="null"/> when the layout has no such entry.</returns>
    public static LayoutPosition? PositionOf(IList<ExplorerEntryData> layout, LayoutEntryId entry)
    {
        ArgumentNullException.ThrowIfNull(layout);
        return SceneLayout.PositionOf(layout, entry);
    }

    /// <summary>Gets the entries of a container, in order.</summary>
    /// <param name="layout">The scene's explorer layout.</param>
    /// <param name="container">Any place in the container; its index is ignored.</param>
    /// <returns>The entries; empty when the layout has no such container.</returns>
    public static IReadOnlyList<LayoutEntryId> EntriesOf(IList<ExplorerEntryData> layout, LayoutPosition container)
    {
        ArgumentNullException.ThrowIfNull(layout);
        ArgumentNullException.ThrowIfNull(container);
        return [.. (SceneLayout.ContainerOf(layout, container) ?? []).Select(SceneLayout.IdOf)];
    }

    /// <summary>Gets the place after the last entry of a container, as a move of some of its entries counts it.</summary>
    /// <remarks>A move takes its index after the moved entries left their places, so entries that move out do not count.</remarks>
    /// <param name="layout">The scene's explorer layout.</param>
    /// <param name="container">Any place in the container; its index is ignored.</param>
    /// <param name="moving">The entries about to move, if any.</param>
    /// <returns>The place at the container's end.</returns>
    public static LayoutPosition EndOf(IList<ExplorerEntryData> layout, LayoutPosition container, IEnumerable<LayoutEntryId>? moving = null)
    {
        ArgumentNullException.ThrowIfNull(layout);
        ArgumentNullException.ThrowIfNull(container);
        var leaving = moving?.ToHashSet() ?? [];
        return container with { Index = EntriesOf(layout, container).Count(entry => !leaving.Contains(entry)) };
    }
}
