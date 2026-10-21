// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Assets;

/// <summary>Projects retained sources and actual published outputs without fabricating subasset identities.</summary>
public sealed partial class AssetFeed
{
    private static bool IsModelSource(AssetPath path) => AssetFileTypes.Is(path.Name, AssetFileCategory.Model);

    private static bool IsProjectCookedOutput(IEnumerable<FileSystemPath> roots, AssetEntry item)
        => item.Kind is AssetKind.Material or AssetKind.Geometry or AssetKind.Scene && roots.Any(root => IsInOutputRoot(item, root));

    private static FileSystemPath? ResolvePublishedOutputPath(AssetEntry item, AssetCookStatus status)
    {
        var output = status.Outputs.FirstOrDefault(output => output.CookedAssetUri == item.Identity);
        return status.HasAvailableOutput && output?.DescriptorRelativePath is { } descriptor
            && status.OutputRoots.TryGetValue(output.MountName, out var root) ? root.Combine(descriptor) : null;
    }

    private static bool IsInOutputRoot(AssetEntry item, FileSystemPath root)
        => item.CookedMetadata is { } metadata ? metadata.ContainerPath == root : item.CookedPath?.IsSameOrInside(root) == true;

    private static AssetEntry ApplyOwnedCookStatus(ProjectContext project, AssetEntry item, AssetCookStatus status)
    {
        if (item.ImportSource is not null || item.DescriptorPath is not null)
        {
            return ApplyCookStatus(item, status);
        }

        var output = status.Outputs.FirstOrDefault(output => output.CookedAssetUri == item.Identity && IsModelSource(output.SourceAssetUri));
        return output is not null && IsProjectCookedOutput(status.OutputRoots.Values, item)
            ? ApplyCookStatus(item with { ImportSource = output.SourceAssetUri, ImportSourcePath = TryResolveSourcePath(project, output.SourceAssetUri) }, status)
            : item;
    }

    private static AssetEntry[] AddKnownImportedOutputs(ProjectContext project, IReadOnlyList<AssetEntry> items, IEnumerable<AssetCookStatus> statuses)
    {
        var rows = items.ToList();
        var known = items.Select(static item => item.Identity).ToHashSet();
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
                var row = new AssetEntry(
                    output.CookedAssetUri,
                    AssetIdentityReducer.GetDisplayName(output.CookedAssetUri),
                    kind,
                    AssetState.Cooked,
                    DerivedState: null,
                    AssetRuntimeAvailability.NotMounted,
                    output.CookedAssetUri.VirtualPath,
                    SourcePath: null,
                    DescriptorPath: null,
                    CookedPath: null,
                    AssetGuid: null,
                    [],
                    IsSelectable: true)
                {
                    ImportSource = output.SourceAssetUri,
                    ImportSourcePath = TryResolveSourcePath(project, output.SourceAssetUri),
                };
                rows.Add(ApplyCookStatus(row, state with { AssetUri = output.CookedAssetUri, HasPublishedOutput = true }));
            }
        }

        return [.. rows];
    }
}
