// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>Shows physical library contents without changing the identities used by ordinary assignments.</summary>
public static class CookedLibraryProjection
{
    /// <summary>Chooses library-specific rows only for explicitly selected local-library scopes.</summary>
    /// <param name="items">The resolved shared asset snapshot.</param>
    /// <param name="project">The current source configuration.</param>
    /// <param name="folders">Selected browser scopes.</param>
    /// <returns>Rows ready for the ordinary folder and query filters.</returns>
    public static IReadOnlyList<ContentBrowserAssetItem> ForFolders(IReadOnlyList<ContentBrowserAssetItem> items, ProjectContext project, IReadOnlyList<string> folders)
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
                    var projected = Create(item, source, project) with { DisplayPath = "/" + library.Name + "/" + source.DescriptorRelativePath };
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

    /// <summary>Creates read-only details for one physical source, retaining the effective assignment source.</summary>
    /// <param name="effective">The asset selected by ordinary native resolution.</param>
    /// <param name="source">The representation explicitly being browsed.</param>
    /// <param name="project">The current library names.</param>
    /// <returns>The library's own read-only asset row.</returns>
    public static ContentBrowserAssetItem Create(ContentBrowserAssetItem effective, CookedAssetMetadata source, ProjectContext project)
    {
        var uri = source.VirtualPath is { } path ? new Uri(AssetUris.Scheme + "://" + path) : effective.CookedUri ?? effective.IdentityUri;
        var physicalPath = AssetIdentityReducer.TryResolveIndexedDescriptorPath(source);
        var exists = physicalPath is not null && File.Exists(physicalPath);
        var library = project.LocalFolderMounts.FirstOrDefault(mount => SameRoot(mount.AbsolutePath, source.RootFolderPath));
        var winningLibrary = project.LocalFolderMounts.FirstOrDefault(mount => effective.CookedMetadata is { } metadata && SameRoot(mount.AbsolutePath, metadata.RootFolderPath));
        var origin = effective.VerifiedBuiltinSources.FirstOrDefault(proof => proof.CookedUri == uri && SameRoot(proof.Root, source.RootFolderPath))?.Origin;
        origin ??= effective.IsBuiltin && effective.CookedMetadata == source ? effective : null;
        return new(
            uri,
            origin?.DisplayName ?? AssetIdentityReducer.GetDisplayName(uri),
            AssetIdentityReducer.GetCookedKind(source.AssetType),
            !exists ? AssetState.Broken : origin is not null ? AssetState.Generated : AssetState.Cooked,
            DerivedState: null,
            effective.RuntimeAvailability,
            library is null ? uri.AbsolutePath : "/" + library.Name + "/" + source.DescriptorRelativePath,
            SourcePath: null,
            DescriptorPath: null,
            uri,
            physicalPath,
            AssetGuid: null,
            exists ? [] : [AssetIdentityDiagnosticCodes.CookedMissing],
            IsSelectable: exists)
        {
            CookedMetadata = source,
            EffectiveCookedSource = effective.CookedMetadata,
            EffectiveCookedSourceName = winningLibrary?.Name ?? "project output",
            RuntimeReason = effective.RuntimeReason,
            Generated = origin?.Generated,
            BuiltinOriginUri = origin?.BuiltinOriginUri ?? origin?.IdentityUri,
            VerifiedBuiltinSources = effective.VerifiedBuiltinSources,
            OverriddenCookedSources = source == effective.CookedMetadata ? effective.OverriddenCookedSources : [],
        };
    }

    private static bool SameRoot(string left, string right) => string.Equals(Path.GetFullPath(left), Path.GetFullPath(right), StringComparison.OrdinalIgnoreCase);

    private static bool IsWithin(string path, string parent) => string.Equals(path, parent, StringComparison.OrdinalIgnoreCase) || path.StartsWith(parent.TrimEnd('/') + "/", StringComparison.OrdinalIgnoreCase);
}
