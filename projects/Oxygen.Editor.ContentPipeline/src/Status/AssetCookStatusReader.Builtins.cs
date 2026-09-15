// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Status;

/// <summary>Reads optional published contributions for engine identities without inventing authored descriptors.</summary>
public sealed partial class AssetCookStatusReader
{
    private static bool IsBuiltinIdentity(Uri uri)
        => uri.IsAbsoluteUri && string.Equals(uri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase)
            && uri.AbsolutePath.StartsWith("/Engine/Generated/", StringComparison.OrdinalIgnoreCase);

    private static Dictionary<Uri, Uri> ResolveBuiltinOutputOrigins(ProjectContext project, CookProvenance provenance)
        => provenance.Products.Where(static product => IsBuiltinIdentity(product.SourceUri))
            .SelectMany(static product => product.Outputs.Select(output => (output.Asset.CookedAssetUri, product.SourceUri)))
            .GroupBy(static output => output.CookedAssetUri)
            .Where(group => group.Select(static output => output.SourceUri).Distinct().Take(2).Count() == 1
                && !CookSavedSourceReader.Exists(CookInputResolver.Resolve(project, group.Key, ContentCookInputRole.Primary).SourceAbsolutePath))
            .ToDictionary(static group => group.Key, static group => group.First().SourceUri);

    private static AssetCookStatus CreateBuiltinStatus(
        Uri uri,
        Dictionary<Uri, CookProvenance.Product> products,
        CookIncrementalPlan plan,
        bool nativeAvailable,
        bool metadataUnavailable)
    {
        _ = products.TryGetValue(uri, out var product);
        var verified = product is not null && VerifyPriorClosure(uri, products, plan.VerifiedOutputs, ImmutableDictionary<Uri, ImmutableArray<CookedDependencySnapshot>>.Empty, []);
        var freshness = metadataUnavailable || !nativeAvailable ? AssetCookFreshness.Unknown
            : product is null ? AssetCookFreshness.NeedsCooking
            : verified && plan.Reusable.ContainsKey(uri) ? AssetCookFreshness.Current : AssetCookFreshness.OutOfDate;
        return new(uri, freshness, product is not null, verified, product is null ? [] : [.. product.Outputs.Select(static output => output.Asset)], [], []);
    }
}
