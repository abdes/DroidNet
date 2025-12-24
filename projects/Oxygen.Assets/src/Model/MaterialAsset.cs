// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Import.Materials;

namespace Oxygen.Assets.Model;

/// <summary>
/// Represents a material asset.
/// </summary>
/// <remarks>
/// This is a minimal implementation for Phase 4. Material properties and metadata
/// will be expanded in future phases.
/// </remarks>
public sealed class MaterialAsset : Asset
{
    /// <summary>
    /// Gets or sets the source data for this material.
    /// </summary>
    /// <remarks>
    /// This property is populated when loading from source (e.g. via <see cref="Resolvers.FileSystemAssetResolver"/>).
    /// It may be null when loading from cooked data if the source is not available or not parsed.
    /// </remarks>
    public MaterialSource? Source { get; set; }
}
