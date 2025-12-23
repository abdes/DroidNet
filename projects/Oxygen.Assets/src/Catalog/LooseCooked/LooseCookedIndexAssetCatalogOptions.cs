// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Catalog.LooseCooked;

/// <summary>
/// Options for <see cref="LooseCookedIndexAssetCatalog"/>.
/// </summary>
public sealed record LooseCookedIndexAssetCatalogOptions
{
    /// <summary>
    /// Gets the cooked root folder path containing <c>container.index.bin</c> and cooked payload files.
    /// </summary>
    public required string CookedRootFolderPath { get; init; }

    /// <summary>
    /// Gets the index file name.
    /// </summary>
    public string IndexFileName { get; init; } = "container.index.bin";

    /// <summary>
    /// Gets the filesystem watcher filter.
    /// </summary>
    /// <remarks>
    /// Defaults to <see cref="IndexFileName"/>.
    /// </remarks>
    public string? WatcherFilter { get; init; }
}
