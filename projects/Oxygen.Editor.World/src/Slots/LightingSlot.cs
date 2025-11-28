// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Slots;

/// <summary>
/// Override slot for lighting properties.
/// </summary>
/// <remarks>
/// Controls shadow casting and receiving for geometry.
/// </remarks>
public partial class LightingSlot : OverrideSlot
{
    private OverridableProperty<bool> castShadows = OverridableProperty.FromDefault(defaultValue: true);
    private OverridableProperty<bool> receiveShadows = OverridableProperty.FromDefault(defaultValue: true);

    static LightingSlot()
    {
        Register<LightingSlotData>(d =>
        {
            var s = new LightingSlot();
            s.Hydrate(d);
            return s;
        });
    }

    /// <summary>
    /// Gets or sets whether the target casts shadows.
    /// </summary>
    /// <value>
    /// An overridable property determining whether the target casts shadows.
    /// Default is <see langword="true"/> (casts shadows).
    /// </value>
    public OverridableProperty<bool> CastShadows
    {
        get => this.castShadows;
        set => this.SetProperty(ref this.castShadows, value);
    }

    /// <summary>
    /// Gets or sets whether the target receives shadows.
    /// </summary>
    /// <value>
    /// An overridable property determining whether the target receives shadows cast by other objects.
    /// Default is <see langword="true"/> (receives shadows).
    /// </value>
    public OverridableProperty<bool> ReceiveShadows
    {
        get => this.receiveShadows;
        set => this.SetProperty(ref this.receiveShadows, value);
    }

    /// <inheritdoc/>
    public override void Hydrate(OverrideSlotData data)
    {
        base.Hydrate(data);

        if (data is LightingSlotData ld)
        {
            using (this.SuppressNotifications())
            {
                this.CastShadows = OverridableProperty.FromDto(this.CastShadows.DefaultValue, ld.CastShadows);
                this.ReceiveShadows = OverridableProperty.FromDto(this.ReceiveShadows.DefaultValue, ld.ReceiveShadows);
            }
        }
    }

    /// <inheritdoc/>
    public override OverrideSlotData Dehydrate()
        => new LightingSlotData
        {
            CastShadows = this.CastShadows.ToDto(),
            ReceiveShadows = this.ReceiveShadows.ToDto(),
        };
}
