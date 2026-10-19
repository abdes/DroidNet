// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Assets;

/// <summary>
///     The identity rules of project assets: how one cooked source presents an asset without changing its identity.
/// </summary>
public static class AssetIdentity
{
    /// <summary>Describes an asset as one cooked source provides it, retaining the source ordinary assignments use.</summary>
    /// <param name="effective">The asset selected by ordinary native resolution.</param>
    /// <param name="source">The representation explicitly being browsed.</param>
    /// <param name="project">The current library names.</param>
    /// <returns>The library's own read-only asset row.</returns>
    public static AssetEntry FromCookedSource(AssetEntry effective, CookedAssetMetadata source, ProjectContext project)
    {
        var identity = source.VirtualPath ?? effective.Identity;
        var physicalPath = AssetIdentityReducer.TryResolveIndexedDescriptorPath(source);
        var exists = source.IsPackaged || (physicalPath is not null && File.Exists(physicalPath.FullPath));
        var library = source.IsPackaged ? null : project.LocalFolderMounts.FirstOrDefault(mount => mount.Root == source.ContainerPath);
        return new(
            identity,
            AssetIdentityReducer.GetDisplayName(identity),
            AssetKinds.FromCookedType(source.AssetType),
            !exists ? AssetState.Broken : source.IsPackaged ? AssetState.Packaged : AssetState.Cooked,
            DerivedState: null,
            effective.RuntimeAvailability,
            library is null ? identity.VirtualPath : "/" + library.Name + "/" + source.DescriptorRelativePath,
            SourcePath: null,
            DescriptorPath: null,
            physicalPath,
            AssetGuid: null,
            exists ? [] : [AssetIdentityDiagnosticCodes.CookedMissing],
            IsSelectable: exists)
        {
            CookedMetadata = source,
            EffectiveCookedSource = effective.CookedMetadata,
            EffectiveCookedSourceName = LibraryName(project, effective) ?? "project output",
            RuntimeReason = effective.RuntimeReason,
            OverriddenCookedSources = source == effective.CookedMetadata ? effective.OverriddenCookedSources : [],
        };
    }

    // The library whose container provides the effective asset: a declared folder or pak, or the engine content pak.
    private static string? LibraryName(ProjectContext project, AssetEntry effective)
    {
        var container = effective.CookedMetadata?.ContainerPath;
        return container is null ? null
            : project.LocalFolderMounts.FirstOrDefault(mount => mount.Root == container)?.Name
                ?? project.PakFileMounts.FirstOrDefault(mount => mount.Path == container)?.Name
                ?? (effective.Origin == AssetOrigin.Engine && effective.IsPackaged ? AssetPath.Engine.Mount : null);
    }
}
