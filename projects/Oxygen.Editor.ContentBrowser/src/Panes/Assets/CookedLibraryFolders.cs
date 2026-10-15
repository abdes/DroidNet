// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Assets;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets;

/// <summary>Shows the contents of browsed cooked-library folders, one row per asset each library provides.</summary>
public static class CookedLibraryFolders
{
    /// <summary>Chooses library-specific rows only for explicitly selected local-library scopes.</summary>
    /// <param name="items">The resolved shared asset snapshot.</param>
    /// <param name="project">The current source configuration.</param>
    /// <param name="folders">Selected browser scopes.</param>
    /// <returns>Rows ready for the ordinary folder and query filters.</returns>
    public static IReadOnlyList<AssetEntry> ForFolders(IReadOnlyList<AssetEntry> items, ProjectContext project, IReadOnlyList<string> folders)
    {
        var libraries = project.LocalFolderMounts.Where(mount => folders.Any(folder => IsWithin(folder, "/" + mount.Name))
            && File.Exists(Path.Combine(mount.AbsolutePath, "container.index.bin"))).ToArray();
        if (libraries.Length == 0)
        {
            return items;
        }

        var includeOrdinary = folders.Any(folder => !libraries.Any(mount => IsWithin(folder, "/" + mount.Name)));
        var rows = includeOrdinary ? items.ToList() : [];
        foreach (var item in items)
        {
            foreach (var source in item.OverriddenCookedSources.Prepend(item.CookedMetadata).OfType<CookedAssetMetadata>())
            {
                foreach (var library in libraries.Where(mount => SameRoot(mount.AbsolutePath, source.RootFolderPath)))
                {
                    var projected = AssetIdentity.FromCookedSource(item, source, project) with { DisplayPath = "/" + library.Name + "/" + source.DescriptorRelativePath };
                    if (folders.Any(folder => IsWithin(projected.DisplayPath, folder)))
                    {
                        rows.Add(projected);
                    }
                }
            }
        }

        var priority = CookedContentOrdering.Resolve(project.LocalFolderMounts, project.CookedContentOrder).Select((source, index) => (source, index)).ToArray();
        return rows.GroupBy(static row => row.IdentityUri).Select(group => group.OrderByDescending(row =>
        {
            var sourceName = project.LocalFolderMounts.FirstOrDefault(mount => row.CookedMetadata is { } metadata && SameRoot(mount.AbsolutePath, metadata.RootFolderPath))?.Name;
            return priority.First(item => sourceName is null ? item.source.Kind == CookedContentSourceKind.ProjectOutput : string.Equals(item.source.Name, sourceName, StringComparison.Ordinal)).index;
        }).First()).ToArray();
    }

    private static bool SameRoot(string left, string right) => string.Equals(Path.GetFullPath(left), Path.GetFullPath(right), StringComparison.OrdinalIgnoreCase);

    private static bool IsWithin(string path, string parent) => string.Equals(path, parent, StringComparison.OrdinalIgnoreCase) || path.StartsWith(parent.TrimEnd('/') + "/", StringComparison.OrdinalIgnoreCase);
}
