// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>
/// Where a new library lands in the content priority: above the libraries it needs and below those that need it, as
/// high as that allows under the project's output, so libraries can be added in any order.
/// </summary>
internal static class LibraryPlacement
{
    /// <summary>Inserts a library into a content priority.</summary>
    /// <param name="order">The priority, lowest first; it holds the project's output.</param>
    /// <param name="name">The new library.</param>
    /// <param name="needs">The libraries it needs.</param>
    /// <param name="neededBy">The libraries that need it.</param>
    /// <returns>The priority with the library in it.</returns>
    public static IReadOnlyList<CookedContentSource> Place(IReadOnlyList<CookedContentSource> order, string name, IReadOnlyCollection<string> needs, IReadOnlyCollection<string> neededBy)
    {
        ArgumentNullException.ThrowIfNull(order);
        var above = -1;
        var below = order.Count;
        var output = order.Count;
        for (var index = 0; index < order.Count; index++)
        {
            var source = order[index];
            if (source.Kind == CookedContentSourceKind.ProjectOutput)
            {
                output = index;
            }
            else if (needs.Contains(source.Name!))
            {
                above = index;
            }
            else if (neededBy.Contains(source.Name!))
            {
                below = Math.Min(below, index);
            }
        }

        // What it needs wins over what needs it: the library works, and the other reports what it lacks.
        var position = Math.Max(Math.Min(output, below), above + 1);
        return [.. order.Take(position), new CookedContentSource(CookedContentSourceKind.Cooked, name), .. order.Skip(position)];
    }
}
