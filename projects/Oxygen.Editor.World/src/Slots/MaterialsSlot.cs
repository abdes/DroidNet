// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Assets;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Slots;

/// <summary>
/// Override slot for material assignments.
/// </summary>
/// <remarks>
/// Allows overriding the material for a specific target (LOD/submesh).
/// When attached to a <see cref="GeometryOverrideTarget"/>, this slot overrides
/// the material for the specified LOD and/or submesh.
/// </remarks>
public partial class MaterialsSlot : OverrideSlot
{
    private AssetReference<MaterialAsset> material = new();

    static MaterialsSlot()
    {
        Register<MaterialsSlotData>(d =>
        {
            var s = new MaterialsSlot();
            s.Hydrate(d);
            return s;
        });
    }

    /// <summary>
    /// Gets or sets the material reference for the target.
    /// </summary>
    /// <value>
    /// An asset reference to the material to apply. The reference can be unresolved (URI only)
    /// or resolved (with Asset instance loaded).
    /// </value>
    public AssetReference<MaterialAsset> Material
    {
        get => this.material;
        set => this.SetProperty(ref this.material, value);
    }

    /// <inheritdoc/>
    public override void Hydrate(OverrideSlotData data)
    {
        base.Hydrate(data);

        if (data is not MaterialsSlotData md)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            if (!string.IsNullOrEmpty(md.MaterialUri))
            {
                this.Material.Uri = md.MaterialUri;
            }
            else
            {
                // Clear override if provided explicitly null/empty
                this.Material.Uri = null;
            }
        }
    }

    /// <inheritdoc/>
    public override OverrideSlotData Dehydrate()
        => new MaterialsSlotData { MaterialUri = this.Material.Uri };
}
