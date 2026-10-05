// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets.Model;
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
    private AssetReference<MaterialAsset> material = new($"{AssetUris.Scheme}:///__uninitialized__");
    private MaterialSlotTarget target = new(new Uri($"{AssetUris.Scheme}:///__uninitialized__"), Guid.Empty, string.Empty);

    static MaterialsSlot()
    {
        Register<MaterialsSlotData>(d =>
        {
            var s = new MaterialsSlot()
            {
                Material = new AssetReference<MaterialAsset>(d.MaterialUri),
                Target = new(d.GeometryUri, d.SlotId, d.LayoutRevision),
            };
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

    /// <summary>Gets or sets the native slot target retained with this assignment.</summary>
    public MaterialSlotTarget Target
    {
        get => this.target;
        set
        {
            ArgumentNullException.ThrowIfNull(value);
            this.SetProperty(ref this.target, value);
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
        this.Target = new(md.GeometryUri, md.SlotId, md.LayoutRevision);
        this.Material = new AssetReference<MaterialAsset>(md.MaterialUri);
    }

    /// <inheritdoc/>
    public override OverrideSlotData Dehydrate()
        => new MaterialsSlotData
        {
            GeometryUri = this.Target.GeometryUri,
            SlotId = this.Target.SlotId,
            LayoutRevision = this.Target.LayoutRevision,
            MaterialUri = this.Material.Uri.ToString(),
        };
}
