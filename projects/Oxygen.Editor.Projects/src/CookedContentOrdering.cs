// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.Projects;

/// <summary>Resolves persisted content priority independently of scan, cook or load completion timing.</summary>
public static class CookedContentOrdering
{
    /// <summary>Expands the saved order with newly declared libraries directly below project output.</summary>
    /// <param name="folders">Local folders in declaration order, oldest first.</param>
    /// <param name="savedOrder">Explicit source priority in mount order, lowest first.</param>
    /// <returns>A complete mount order; callers omit folders that do not contain cooked content.</returns>
    public static IReadOnlyList<CookedContentSource> Resolve(IEnumerable<LocalFolderMount> folders, IEnumerable<CookedContentSource> savedOrder)
    {
        ArgumentNullException.ThrowIfNull(folders);
        ArgumentNullException.ThrowIfNull(savedOrder);
        var definitions = folders.ToArray();
        if (definitions.Any(static folder => folder is null || string.IsNullOrWhiteSpace(folder.Name)))
        {
            throw new InvalidDataException("Content priority requires named folder declarations.");
        }

        var declared = definitions.ToDictionary(static folder => folder.Name, StringComparer.OrdinalIgnoreCase);
        var order = new List<CookedContentSource>();
        var included = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var projectIndex = -1;
        foreach (var source in savedOrder)
        {
            if (source is null)
            {
                throw new InvalidDataException("Content priority contains an empty source entry.");
            }

            if (source.Kind == CookedContentSourceKind.ProjectOutput && source.Name is null && projectIndex < 0)
            {
                projectIndex = order.Count;
                order.Add(source);
            }
            else if (source.Kind == CookedContentSourceKind.LocalFolder && source.Name is { } name
                && declared.TryGetValue(name, out var folder) && included.Add(folder.Name))
            {
                order.Add(new(CookedContentSourceKind.LocalFolder, folder.Name));
            }
            else
            {
                throw new InvalidDataException("Content priority contains a duplicate, invalid or undeclared source.");
            }
        }

        if (projectIndex < 0)
        {
            projectIndex = order.Count;
            order.Add(new(CookedContentSourceKind.ProjectOutput));
        }

        foreach (var folder in definitions.Where(folder => !included.Contains(folder.Name)))
        {
            order.Insert(projectIndex++, new(CookedContentSourceKind.LocalFolder, folder.Name));
        }

        return order;
    }
}
