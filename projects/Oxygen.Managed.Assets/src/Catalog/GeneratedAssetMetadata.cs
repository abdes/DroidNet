// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets.Catalog;

/// <summary>Engine-provided recipe identity and its default cooked contribution.</summary>
/// <param name="CanonicalName">The generator's canonical name, shared by aliases.</param>
/// <param name="DescriptorSchema">The engine descriptor schema identifying the recipe version.</param>
/// <param name="CookedVirtualPath">The recipe's runtime output path in the requested mount.</param>
public sealed record GeneratedAssetMetadata(string CanonicalName, string DescriptorSchema, string CookedVirtualPath)
{
    /// <summary>Gets a value indicating whether discovery is using a previously captured engine catalog.</summary>
    public bool IsLastKnown { get; init; }
}
