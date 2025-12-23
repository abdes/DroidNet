// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Catalog;

/// <summary>
/// Indicates the type of a catalog change.
/// </summary>
public enum AssetChangeKind
{
    /// <summary>
    /// An asset became visible in the catalog.
    /// </summary>
    Added,

    /// <summary>
    /// An asset is no longer visible in the catalog.
    /// </summary>
    Removed,

    /// <summary>
    /// An asset's data changed but its identity remained stable.
    /// </summary>
    Updated,

    /// <summary>
    /// An asset changed identity (rename/move). <see cref="AssetChange.PreviousUri"/> carries the old value.
    /// </summary>
    Relocated,
}

/// <summary>
/// Represents an incremental change in an asset catalog.
/// </summary>
public sealed record AssetChange(AssetChangeKind Kind, Uri Uri, Uri? PreviousUri = null);
