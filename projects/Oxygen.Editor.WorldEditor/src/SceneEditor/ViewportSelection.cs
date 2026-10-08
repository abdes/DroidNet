// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.LevelEditor;

/// <summary>How a viewport pick combines with the current selection.</summary>
public enum ViewportSelectionMode
{
    /// <summary>The picked nodes replace the selection; picking nothing clears it.</summary>
    Replace,

    /// <summary>The picked nodes are added to the selection (Shift).</summary>
    Add,

    /// <summary>Each picked node is added when unselected and removed when selected (Ctrl).</summary>
    Toggle,
}

/// <summary>The scene nodes a viewport click or marquee picked, and how they combine with the selection.</summary>
/// <param name="NodeIds">The picked nodes, the one nearest the pointer or marquee centre last.</param>
/// <param name="Mode">How the picked nodes combine with the current selection.</param>
public sealed record ViewportSelection(IReadOnlyList<Guid> NodeIds, ViewportSelectionMode Mode)
{
    /// <summary>
    /// Combines the picked nodes with the current selection. The last node of the result is the
    /// active one.
    /// </summary>
    /// <param name="current">The current selection, in selection order.</param>
    /// <returns>
    ///     The new selection, or <see langword="null"/> when the pick leaves the selection unchanged:
    ///     an empty Add or Toggle pick.
    /// </returns>
    public IReadOnlyList<Guid>? Combine(IReadOnlyList<Guid> current)
    {
        ArgumentNullException.ThrowIfNull(current);
        var picked = this.NodeIds.Distinct().ToList();
        if (picked.Count == 0)
        {
            return this.Mode == ViewportSelectionMode.Replace && current.Count > 0 ? [] : null;
        }

        switch (this.Mode)
        {
            case ViewportSelectionMode.Add:
                return [.. current.Where(id => !picked.Contains(id)), .. picked];
            case ViewportSelectionMode.Toggle:
                var removed = picked.Where(current.Contains).ToHashSet();
                return [.. current.Where(id => !removed.Contains(id)), .. picked.Where(id => !removed.Contains(id))];
            default:
                return picked;
        }
    }
}
