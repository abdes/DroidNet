// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Policies;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Slots;

/// <summary>
/// Override slot for Level of Detail (LOD) configuration.
/// </summary>
/// <remarks>
/// Allows overriding the LOD selection policy for geometry rendering.
/// This determines which LOD level is used based on distance, screen space error, or a fixed index.
/// </remarks>
public partial class LevelOfDetailSlot : OverrideSlot
{
    private OverridableProperty<LodPolicy> lodPolicy = OverridableProperty.FromDefault<LodPolicy>(new FixedLodPolicy());

    static LevelOfDetailSlot()
    {
        Register<LevelOfDetailSlotData>(d =>
        {
            var s = new LevelOfDetailSlot();
            s.Hydrate(d);
            return s;
        });
    }

    /// <summary>
    /// Gets or sets the LOD selection policy.
    /// </summary>
    /// <value>
    /// An overridable property containing the LOD policy to use.
    /// Default is <see cref="FixedLodPolicy"/> with LOD index 0 (highest detail).
    /// </value>
    public OverridableProperty<LodPolicy> LodPolicy
    {
        get => this.lodPolicy;
        set => this.SetProperty(ref this.lodPolicy, value);
    }

    /// <inheritdoc/>
    public override void Hydrate(OverrideSlotData data)
    {
        base.Hydrate(data);

        if (data is LevelOfDetailSlotData ld)
        {
            using (this.SuppressNotifications())
            {
                this.LodPolicy = OverridableProperty.FromDto(this.LodPolicy.DefaultValue, ld.LodPolicy);
            }
        }
    }

    /// <inheritdoc/>
    public override OverrideSlotData Dehydrate()
        => new LevelOfDetailSlotData { LodPolicy = this.LodPolicy.ToDto() };
}
