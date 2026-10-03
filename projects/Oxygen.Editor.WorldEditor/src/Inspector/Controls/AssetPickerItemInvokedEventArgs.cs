// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Inspector.Geometry;

namespace Oxygen.Editor.World.Inspector.Controls;

/// <summary>Identifies the existing stable geometry or material row chosen in a catalog.</summary>
public sealed class AssetPickerItemInvokedEventArgs : EventArgs
{
    /// <summary>
    /// Initializes a new instance of the <see cref="AssetPickerItemInvokedEventArgs"/> class for a geometry choice.
    /// </summary>
    /// <param name="asset">The original stable geometry row.</param>
    internal AssetPickerItemInvokedEventArgs(AssetPickerRow asset) => this.Asset = asset;

    /// <summary>
    /// Initializes a new instance of the <see cref="AssetPickerItemInvokedEventArgs"/> class for a material choice,
    /// including explicit None.
    /// </summary>
    /// <param name="material">The original stable material row.</param>
    internal AssetPickerItemInvokedEventArgs(MaterialPickerRow material) => this.Material = material;

    /// <summary>Gets the geometry choice, or null for a material invocation.</summary>
    public AssetPickerRow? Asset { get; }

    /// <summary>Gets the material choice, or null for a geometry invocation.</summary>
    public MaterialPickerRow? Material { get; }
}
