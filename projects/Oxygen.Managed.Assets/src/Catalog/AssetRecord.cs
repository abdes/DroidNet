// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Catalog;

/// <summary>
/// Minimal asset listing record returned by the catalog.
/// </summary>
/// <remarks>
/// Catalog records are intentionally lightweight. Consumers that need the full asset metadata
/// use the native content pipeline for runtime loading.
/// </remarks>
/// <param name="Identity">The asset's identity.</param>
public sealed record AssetRecord(AssetPath Identity)
{
    /// <summary>Gets engine recipe metadata when this row represents a generated asset.</summary>
    public GeneratedAssetMetadata? Generated { get; init; }

    /// <summary>Gets indexed cooked identity, type and location without inferring them from the virtual filename.</summary>
    public CookedAssetMetadata? Cooked { get; init; }

    /// <summary>Gets lower-priority indexed representations masked by this asset's resolved source.</summary>
    public IReadOnlyList<CookedAssetMetadata> OverriddenCookedSources { get; init; } = [];

    /// <summary>Gets the asset's file name without its extension.</summary>
    public string Name => Path.GetFileNameWithoutExtension(this.Identity.Name);
}
