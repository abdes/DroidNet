// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>Projects retained sources and actual published outputs without fabricating subasset identities.</summary>
public sealed partial class ContentBrowserAssetProvider
{
    private static bool IsModelSource(Uri uri) => Path.GetExtension(uri.AbsolutePath).ToUpperInvariant() is ".GLTF" or ".GLB" or ".FBX";

    private static bool IsProjectCookedOutput(ProjectContext project, ContentBrowserAssetItem item)
        => item.Kind is AssetKind.Material or AssetKind.Geometry or AssetKind.Scene
            && item.CookedUri is not null && TryResolveSourcePath(project, item.CookedUri) is not null
            && project.AuthoringMounts.Any(mount => IsInOutputRoot(item, Path.GetFullPath(Path.Combine(project.ProjectRoot, ".cooked", mount.Name))));

    private static bool IsInOutputRoot(ContentBrowserAssetItem item, string root)
        => item.CookedMetadata is { } metadata ? string.Equals(Path.GetFullPath(metadata.RootFolderPath), root, StringComparison.OrdinalIgnoreCase)
            : item.CookedPath is { } path && Path.GetFullPath(path).StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);

    private static ContentBrowserAssetItem ApplyOwnedCookStatus(ProjectContext project, ContentBrowserAssetItem item, AssetCookStatus status)
    {
        if (item.ImportSourceUri is not null || item.DescriptorPath is not null)
        {
            return ApplyCookStatus(item, status);
        }

        var output = status.Outputs.FirstOrDefault(output => output.CookedAssetUri == item.IdentityUri && IsModelSource(output.SourceAssetUri));
        return output is not null && IsProjectCookedOutput(project, item)
            ? ApplyCookStatus(item with { ImportSourceUri = output.SourceAssetUri, ImportSourcePath = TryResolveSourcePath(project, output.SourceAssetUri) }, status)
            : item;
    }

    private static ContentBrowserAssetItem[] AddKnownImportedOutputs(ProjectContext project, IReadOnlyList<ContentBrowserAssetItem> items, IEnumerable<AssetCookStatus> statuses)
    {
        var rows = items.ToList();
        var known = items.SelectMany(static item => new[] { item.IdentityUri, item.CookedUri }).OfType<Uri>().ToHashSet();
        foreach (var state in statuses)
        {
            foreach (var output in state.Outputs.Where(static output => IsModelSource(output.SourceAssetUri)))
            {
                if (!known.Add(output.CookedAssetUri))
                {
                    continue;
                }

                var kind = output.Kind switch
                {
                    ContentCookAssetKind.Geometry => AssetKind.Geometry,
                    ContentCookAssetKind.Material => AssetKind.Material,
                    ContentCookAssetKind.Scene => AssetKind.Scene,
                    _ => AssetKind.Unknown,
                };
                var row = new ContentBrowserAssetItem(
                    output.CookedAssetUri,
                    AssetIdentityReducer.GetDisplayName(output.CookedAssetUri),
                    kind,
                    AssetState.Cooked,
                    DerivedState: null,
                    AssetRuntimeAvailability.NotMounted,
                    AssetUriHelper.GetVirtualPath(output.CookedAssetUri),
                    SourcePath: null,
                    DescriptorPath: null,
                    output.CookedAssetUri,
                    CookedPath: null,
                    AssetGuid: null,
                    [],
                    IsSelectable: true)
                {
                    ImportSourceUri = output.SourceAssetUri,
                    ImportSourcePath = TryResolveSourcePath(project, output.SourceAssetUri),
                };
                rows.Add(ApplyCookStatus(row, state with { AssetUri = output.CookedAssetUri, HasPublishedOutput = true }));
            }
        }

        return rows.ToArray();
    }
}
