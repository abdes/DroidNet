// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Authoring.Materials;

namespace Oxygen.Managed.Assets.Model;

/// <summary>
/// Represents a material asset.
/// </summary>
public sealed class MaterialAsset : Asset
{
    /// <summary>
    /// Gets or sets the source data for this material.
    /// </summary>
    /// <remarks>
    /// Authoring services populate this from a source descriptor. A catalog-only
    /// cooked representation has no source data attached.
    /// </remarks>
    public MaterialSource? Source { get; set; }
}
