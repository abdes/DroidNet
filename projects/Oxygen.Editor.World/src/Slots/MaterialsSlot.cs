// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Slots;

/// <summary>
/// Override slot for material assignments.
/// </summary>
/// <remarks>
/// The native slot inventory determines every affected LOD/submesh binding.
/// The retained geometry and slot identities prevent reimport from rebinding by index.
/// </remarks>
public partial class MaterialsSlot : OverrideSlot
{
    static MaterialsSlot()
    {
        Register<MaterialsSlotData>(d =>
        {
            var s = new MaterialsSlot()
            {
                Material = AssetPathData.Require(d.MaterialUri, nameof(d.MaterialUri)),
                Target = ReadTarget(d),
            };
            s.Hydrate(d);
            return s;
        });
    }

    /// <summary>
    /// Gets or sets the material asset used instead of the mesh's own material on the target slot.
    /// </summary>
    /// <exception cref="ArgumentNullException">The value is <see langword="null"/>.</exception>
    public required AssetPath Material
    {
        get;
        set => _ = this.SetProperty(ref field, value ?? throw new ArgumentNullException(nameof(value)));
    }

    /// <summary>Gets or sets the native slot target retained with this assignment.</summary>
    /// <exception cref="ArgumentNullException">The value is <see langword="null"/>.</exception>
    public required MaterialSlotTarget Target
    {
        get;
        set
        {
            ArgumentNullException.ThrowIfNull(value);
            _ = this.SetProperty(ref field, value);
        }
    }

    /// <inheritdoc/>
    public override void Hydrate(OverrideSlotData data)
    {
        base.Hydrate(data);

        if (data is not MaterialsSlotData md)
        {
            return;
        }

        using var notifications = this.SuppressNotifications();
        this.Target = ReadTarget(md);
        this.Material = AssetPathData.Require(md.MaterialUri, nameof(md.MaterialUri));
    }

    /// <inheritdoc/>
    public override OverrideSlotData Dehydrate()
        => new MaterialsSlotData
        {
            GeometryUri = this.Target.Geometry,
            SlotId = this.Target.SlotId,
            LayoutRevision = this.Target.LayoutRevision,
            MaterialUri = this.Material,
        };

    private static MaterialSlotTarget ReadTarget(MaterialsSlotData data)
        => new(AssetPathData.Require(data.GeometryUri, nameof(data.GeometryUri)), data.SlotId, data.LayoutRevision);
}
