// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Assets;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using LooseCookedIndex = Oxygen.Managed.Assets.Persistence.LooseCooked.LooseCookedIndex;

namespace Oxygen.Editor.ContentExplorer.Panes.Assets;

/// <summary>Shows the contents of browsed library mounts, one row per asset each library provides.</summary>
public static class CookedLibraryFolders
{
    /// <summary>Chooses library-specific rows only for explicitly selected library scopes: local folders and pak files.</summary>
    /// <param name="items">The resolved shared asset snapshot.</param>
    /// <param name="project">The current source configuration.</param>
    /// <param name="folders">Selected browser scopes.</param>
    /// <returns>Rows ready for the ordinary folder and query filters.</returns>
    public static IReadOnlyList<AssetEntry> ForFolders(IReadOnlyList<AssetEntry> items, ProjectContext project, IReadOnlyCollection<AssetPath> folders)
    {
        var libraries = Library.Of(project).Where(library => folders.Any(folder => string.Equals(folder.Mount, library.Name, StringComparison.Ordinal))
            && library.HasContent(project)).ToArray();
        if (libraries.Length == 0)
        {
            return items;
        }

        var includeOrdinary = folders.Any(folder => !libraries.Any(library => string.Equals(folder.Mount, library.Name, StringComparison.Ordinal)));
        var rows = includeOrdinary ? items.ToList() : [];
        foreach (var item in items)
        {
            foreach (var source in item.OverriddenCookedSources.Prepend(item.CookedMetadata).OfType<CookedAssetMetadata>())
            {
                foreach (var library in libraries.Where(library => library.Container == source.ContainerPath))
                {
                    if (library.Locate(source) is { } location && folders.Any(location.IsSameOrInside))
                    {
                        rows.Add(AssetIdentity.FromCookedSource(item, source, project) with { DisplayPath = location.VirtualPath });
                    }
                }
            }
        }

        var priority = CookedContentOrdering.Resolve(project).Select((source, index) => (source, index)).ToArray();
        var all = Library.Of(project);
        return [.. rows.GroupBy(static row => row.Identity).Select(group => group.OrderByDescending(row =>
        {
            var owner = all.FirstOrDefault(library => library.Container == row.CookedMetadata?.ContainerPath);
            return priority.First(item => owner is null ? item.source.Kind == CookedContentSourceKind.ProjectOutput : string.Equals(item.source.Name, owner.Name, StringComparison.Ordinal)).index;
        }).First())];
    }

    // A local folder shows its assets at their descriptor paths, a pak at its assets' virtual paths, under the mount.
    private sealed record Library(bool IsPak, string Name, FileSystemPath Container)
    {
        public bool HasContent(ProjectContext project) => this.IsPak
            ? project.FileSystem.File.Exists(this.Container.FullPath)
            : project.FileSystem.File.Exists(Path.Combine(this.Container.FullPath, LooseCookedIndex.FileName));

        public static Library[] Of(ProjectContext project)
            => [.. project.LibraryMounts.Select(static mount => new Library(mount.Container == MountContainer.Pak, mount.Name, mount.Location!))];

        public AssetPath? Locate(CookedAssetMetadata source)
        {
            var relative = this.IsPak ? source.VirtualPath?.VirtualPath.TrimStart('/') : source.DescriptorRelativePath;
            return AssetPath.TryCreate(this.Name, relative, out var location) ? location : null;
        }
    }
}
