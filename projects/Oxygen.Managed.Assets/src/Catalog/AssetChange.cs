// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Catalog;

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
    /// An asset changed identity (rename/move). <see cref="AssetChange.PreviousIdentity"/> carries the old value.
    /// </summary>
    Relocated,

    /// <summary>
    /// An asset became visible because the catalog started listing it, as when a project opens or a folder is added;
    /// the asset itself did not change.
    /// </summary>
    Listed,
}

/// <summary>
/// Represents an incremental change in an asset catalog.
/// </summary>
/// <param name="Kind">What changed.</param>
/// <param name="Identity">The asset that changed.</param>
/// <param name="PreviousIdentity">The asset's identity before a <see cref="AssetChangeKind.Relocated"/> change.</param>
public sealed record AssetChange(AssetChangeKind Kind, AssetPath Identity, AssetPath? PreviousIdentity = null);
