// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Slots;

/// <summary>
/// Override slot for rendering properties.
/// </summary>
/// <remarks>
/// Controls visibility and other rendering-related flags for geometry.
/// </remarks>
public partial class RenderingSlot : OverrideSlot
{
    private OverridableProperty<bool> isVisible = OverridableProperty.FromDefault(defaultValue: true);

    static RenderingSlot()
    {
        Register<RenderingSlotData>(d =>
        {
            var s = new RenderingSlot();
            s.Hydrate(d);
            return s;
        });
    }

    /// <summary>
    /// Gets or sets the visibility override for the target.
    /// </summary>
    /// <value>
    /// An overridable property determining whether the target is visible.
    /// Default is <see langword="true"/> (visible).
    /// </value>
    public OverridableProperty<bool> IsVisible
    {
        get => this.isVisible;
        set => this.SetProperty(ref this.isVisible, value);
    }

    /// <inheritdoc/>
    public override void Hydrate(OverrideSlotData data)
    {
        base.Hydrate(data);

        if (data is RenderingSlotData rd)
        {
            using (this.SuppressNotifications())
            {
                this.IsVisible = OverridableProperty.FromDto(this.IsVisible.DefaultValue, rd.IsVisible);
            }
        }
    }

    /// <inheritdoc/>
    public override OverrideSlotData Dehydrate()
        => new RenderingSlotData { IsVisible = this.IsVisible.ToDto() };
}
