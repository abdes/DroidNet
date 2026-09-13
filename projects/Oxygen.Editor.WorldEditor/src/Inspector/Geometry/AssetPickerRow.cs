// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace Oxygen.Editor.World.Inspector.Geometry;

/// <summary>Keeps a geometry choice's container and focus stable while its status changes.</summary>
/// <param name="item">The initial immutable choice.</param>
public sealed partial class AssetPickerRow(AssetPickerItem item) : ObservableObject
{
    private AssetPickerItem item = item;

    /// <summary>Gets the current geometry choice.</summary>
    public AssetPickerItem Item => this.item;

    /// <summary>Updates this identity's presentation.</summary>
    /// <param name="replacement">The latest choice for this URI.</param>
    internal void Update(AssetPickerItem replacement)
    {
        if (this.item.Uri != replacement.Uri)
        {
            throw new ArgumentException("A picker row can only update its own geometry identity.", nameof(replacement));
        }

        _ = this.SetProperty(ref this.item, replacement, nameof(this.Item));
    }
}
