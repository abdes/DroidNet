// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using Oxygen.Editor.Assets;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.World;

/// <summary>
/// Component that references and configures geometry assets for rendering.
/// </summary>
/// <remarks>
/// <para>
/// The <see cref="GeometryComponent"/> allows scene nodes to reference geometry assets (meshes)
/// and configure how they are rendered. It supports:
/// </para>
/// <list type="bullet">
/// <item><description>Global overrides (via <see cref="GameComponent.Node"/>'s <see cref="GameObject.OverrideSlots"/>)</description></item>
/// <item><description>Component-level overrides (via <see cref="Slots.OverrideSlot"/> instances directly on this component)</description></item>
/// <item><description>Targeted overrides (via <see cref="TargetedOverrides"/> for specific LODs/submeshes)</description></item>
/// </list>
/// </remarks>
public partial class GeometryComponent : GameComponent
{
    private AssetReference<GeometryAsset> geometry = new();

    static GeometryComponent()
    {
        Register<GeometryComponentData>(d =>
        {
            var g = new GeometryComponent { Name = d.Name };
            g.Hydrate(d);
            return g;
        });
    }

    /// <summary>
    /// Gets or sets the geometry asset reference.
    /// </summary>
    /// <value>
    /// An asset reference to the geometry to render. The reference can be unresolved (URI only)
    /// or resolved (with Asset instance loaded).
    /// </value>
    public AssetReference<GeometryAsset> Geometry
    {
        get => this.geometry;
        set => this.SetProperty(ref this.geometry, value);
    }

    /// <summary>
    /// Gets the collection of targeted overrides for specific LODs and/or submeshes.
    /// </summary>
    /// <remarks>
    /// Targeted overrides allow fine-grained control over rendering properties for specific parts
    /// of the geometry. Each <see cref="GeometryOverrideTarget"/> specifies which LOD and submesh
    /// to target, with -1 meaning "all".
    /// </remarks>
    public ObservableCollection<GeometryOverrideTarget> TargetedOverrides { get; } = [];

    /// <summary>
    /// Gets component-level override slots for this geometry component.
    /// </summary>
    public ObservableCollection<OverrideSlot> OverrideSlots { get; } = [];

    /// <inheritdoc/>
    public override void Hydrate(ComponentData data)
    {
        base.Hydrate(data);

        if (data is not GeometryComponentData gd)
        {
            return;
        }

        using (this.SuppressNotifications())
        {
            // geometry URI
            this.Geometry.Uri = gd.GeometryUri;

            // component-level override slots
            this.OverrideSlots.Clear();
            foreach (var slotDto in gd.OverrideSlots)
            {
                var slot = OverrideSlot.CreateAndHydrate(slotDto);
                this.OverrideSlots.Add(slot);
            }

            // targeted overrides
            this.TargetedOverrides.Clear();
            foreach (var targ in gd.TargetedOverrides)
            {
                var t = new GeometryOverrideTarget { LodIndex = targ.LodIndex, SubmeshIndex = targ.SubmeshIndex };
                foreach (var s in targ.OverrideSlots)
                {
                    var slot = OverrideSlot.CreateAndHydrate(s);
                    t.OverrideSlots.Add(slot);
                }

                this.TargetedOverrides.Add(t);
            }
        }
    }

    /// <inheritdoc/>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0305:Simplify collection initialization", Justification = "I find .ToList() is clearer with LINQ")]
    public override ComponentData Dehydrate()
        => new GeometryComponentData
        {
            Name = this.Name,
            GeometryUri = this.Geometry.Uri,
            OverrideSlots = this.OverrideSlots.Select(s => s.Dehydrate()).ToList(),
            TargetedOverrides = this.TargetedOverrides.Select(t => new TargetedOverrideData
            {
                LodIndex = t.LodIndex,
                SubmeshIndex = t.SubmeshIndex,
                OverrideSlots = t.OverrideSlots.Select(s => s.Dehydrate()).ToList(),
            }).ToList(),
        };
}
