// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Status;

/// <summary>Reads optional published contributions for engine identities without inventing authored descriptors.</summary>
public sealed partial class AssetCookStatusReader
{
    private static bool IsBuiltinIdentity(Uri uri)
        => uri.IsAbsoluteUri && string.Equals(uri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase)
            && uri.AbsolutePath.StartsWith("/Engine/Generated/", StringComparison.OrdinalIgnoreCase);

    private static AssetCookStatus CreateBuiltinStatus(
        Uri uri,
        Dictionary<Uri, CookProvenance.Product> products,
        CookIncrementalPlan plan,
        bool nativeAvailable,
        bool metadataUnavailable)
    {
        _ = products.TryGetValue(uri, out var product);
        var verified = product is not null && VerifyPriorClosure(uri, products, plan.VerifiedOutputs, []);
        var freshness = metadataUnavailable || !nativeAvailable ? AssetCookFreshness.Unknown
            : product is null ? AssetCookFreshness.NeedsCooking
            : verified && plan.Reusable.ContainsKey(uri) ? AssetCookFreshness.Current : AssetCookFreshness.OutOfDate;
        return new(uri, freshness, product is not null, verified, product is null ? [] : [.. product.Outputs.Select(static output => output.Asset)], [], []);
    }
}
