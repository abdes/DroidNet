// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Assets;

/// <summary>
///     The identity rules of project assets: which references denote the same asset, how built-ins group with their
///     verified cooked copies, and how one cooked source presents an asset without changing its identity.
/// </summary>
public static class AssetIdentity
{
    /// <summary>Returns one entry per logical identity among the supplied entries.</summary>
    /// <param name="items">Rows already restricted to the relevant folder scope.</param>
    /// <returns>Canonical built-ins with their verified cooked companions, and all independent assets.</returns>
    public static IReadOnlyList<AssetEntry> GroupBuiltins(IEnumerable<AssetEntry> items)
        => items.GroupBy(static item => (item.Kind, Identity: item.IsBuiltin ? item.BuiltinOrigin ?? item.Identity : item.Identity))
            .Select(static group => Combine(group.Key.Identity, group.ToArray())).ToArray();

    /// <summary>Checks both the logical identity and its proven existing reference aliases.</summary>
    /// <param name="item">The projected logical asset.</param>
    /// <param name="identity">The reference being selected or invoked.</param>
    /// <returns>Whether this row represents that reference.</returns>
    public static bool Represents(AssetEntry item, AssetPath identity)
        => item.Identity == identity || item.CookedCompanions.Any(companion => companion.Identity == identity);

    /// <summary>Describes an asset as one cooked source provides it, retaining the source ordinary assignments use.</summary>
    /// <param name="effective">The asset selected by ordinary native resolution.</param>
    /// <param name="source">The representation explicitly being browsed.</param>
    /// <param name="project">The current library names.</param>
    /// <returns>The library's own read-only asset row.</returns>
    public static AssetEntry FromCookedSource(AssetEntry effective, CookedAssetMetadata source, ProjectContext project)
    {
        var identity = source.VirtualPath ?? effective.Identity;
        var physicalPath = AssetIdentityReducer.TryResolveIndexedDescriptorPath(source);
        var exists = physicalPath is not null && File.Exists(physicalPath.FullPath);
        var library = project.LocalFolderMounts.FirstOrDefault(mount => mount.Root == source.RootFolderPath);
        var winningLibrary = project.LocalFolderMounts.FirstOrDefault(mount => mount.Root == effective.CookedMetadata?.RootFolderPath);
        var origin = effective.VerifiedBuiltinSources.FirstOrDefault(proof => proof.Identity == identity && proof.Root == source.RootFolderPath)?.Origin;
        origin ??= effective.IsBuiltin && effective.CookedMetadata == source ? effective : null;
        return new(
            identity,
            origin?.DisplayName ?? AssetIdentityReducer.GetDisplayName(identity),
            AssetKinds.FromCookedType(source.AssetType),
            !exists ? AssetState.Broken : origin is not null ? AssetState.Generated : AssetState.Cooked,
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
            EffectiveCookedSourceName = winningLibrary?.Name ?? "project output",
            RuntimeReason = effective.RuntimeReason,
            Generated = origin?.Generated,
            BuiltinOrigin = origin?.BuiltinOrigin ?? origin?.Identity,
            VerifiedBuiltinSources = effective.VerifiedBuiltinSources,
            OverriddenCookedSources = source == effective.CookedMetadata ? effective.OverriddenCookedSources : [],
        };
    }

    private static AssetEntry Combine(AssetPath identity, AssetEntry[] group)
    {
        var primary = group.FirstOrDefault(item => item.Identity == identity) ?? group[0];
        if (group.Length == 1)
        {
            return primary;
        }

        var companions = group.Where(item => !ReferenceEquals(item, primary)).ToArray();
        var runtime = group.FirstOrDefault(static item => item.RuntimeAvailability == AssetRuntimeAvailability.Failed)
            ?? group.FirstOrDefault(static item => item.RuntimeAvailability == AssetRuntimeAvailability.Updating)
            ?? primary;
        return primary with
        {
            CookedCompanions = companions,
            RuntimeAvailability = runtime.RuntimeAvailability,
            RuntimeReason = runtime.RuntimeReason,
            DiagnosticCodes = group.SelectMany(static item => item.DiagnosticCodes).Distinct(StringComparer.Ordinal).ToArray(),
        };
    }
}
