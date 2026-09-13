// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace Oxygen.Editor.World.Inspector.Geometry;

/// <summary>Preserves a material choice's visual container and focus during status updates.</summary>
/// <param name="item">The initial choice.</param>
public sealed partial class MaterialPickerRow(MaterialPickerItem item) : ObservableObject
{
    private MaterialPickerItem item = item;

    /// <summary>Gets the latest immutable choice, including its current availability.</summary>
    public MaterialPickerItem Item => this.item;

    /// <summary>Updates the choice without changing its material identity.</summary>
    /// <param name="replacement">The latest presentation of this material.</param>
    internal void Update(MaterialPickerItem replacement)
    {
        if (this.item.Uri != replacement.Uri)
        {
            throw new ArgumentException("A picker row can only update its own material identity.", nameof(replacement));
        }

        _ = this.SetProperty(ref this.item, replacement, nameof(this.Item));
    }
}
