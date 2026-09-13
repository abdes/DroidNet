// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Model;

namespace Oxygen.Managed.Assets.Catalog;

/// <summary>
/// Minimal asset listing record returned by the catalog.
/// </summary>
/// <remarks>
/// Catalog records are intentionally lightweight. Consumers that need the full asset metadata
/// should load the asset via <see cref="IAssetService"/>.
/// </remarks>
public sealed record AssetRecord(Uri Uri)
{
    /// <summary>Gets engine recipe metadata when this row represents a generated asset.</summary>
    public GeneratedAssetMetadata? Generated { get; init; }

    /// <summary>Gets indexed cooked identity, type and location without inferring them from the virtual filename.</summary>
    public CookedAssetMetadata? Cooked { get; init; }

    /// <summary>Gets lower-priority indexed representations masked by this asset's resolved source.</summary>
    public IReadOnlyList<CookedAssetMetadata> OverriddenCookedSources { get; init; } = [];

    /// <summary>
    /// Gets the asset name derived from the URI path.
    /// </summary>
    public string Name => Path.GetFileNameWithoutExtension(this.Uri.AbsolutePath);
}
