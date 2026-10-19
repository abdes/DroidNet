// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Catalog;

/// <summary>Builds the catalog records of one cooked container, keyed by the virtual paths it lists.</summary>
internal sealed class CookedAssetRecords
{
    private readonly Dictionary<AssetPath, AssetRecord> records = [];

    /// <summary>Gets the records added so far.</summary>
    public IReadOnlyDictionary<AssetPath, AssetRecord> Records => this.records;

    /// <summary>Adds the record of an asset the container lists under <paramref name="virtualPath"/>.</summary>
    /// <param name="virtualPath">The virtual path the container records.</param>
    /// <param name="metadata">Creates the cooked metadata of the parsed identity.</param>
    /// <exception cref="InvalidDataException">The path is not an asset's virtual path, or the container lists it twice.</exception>
    public void Add(string virtualPath, Func<AssetPath, CookedAssetMetadata> metadata)
    {
        if (!AssetPath.TryParse(virtualPath, out var identity) || identity.IsMount)
        {
            throw new InvalidDataException($"The cooked container lists '{virtualPath}', which is not the virtual path of an asset.");
        }

        if (!this.records.TryAdd(identity, new AssetRecord(identity) { Cooked = metadata(identity) }))
        {
            throw new InvalidDataException($"The cooked container lists the virtual path '{virtualPath}' twice.");
        }
    }
}
