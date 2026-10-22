// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// Component that references and configures geometry assets for rendering.
/// </summary>
/// <remarks>
/// <para>
/// The <see cref="GeometryComponent"/> allows scene nodes to reference geometry assets (meshes)
/// and configure how they are rendered. It supports:
/// </para>
/// <list type="bullet">
/// <item><description>Global overrides (via <see cref="GameComponent.Node"/>'s <see cref="SceneNode.OverrideSlots"/>)</description></item>
/// <item><description>Component-level overrides (via <see cref="Slots.OverrideSlot"/> instances directly on this component)</description></item>
/// <item><description>Targeted overrides (via <see cref="TargetedOverrides"/> for specific LODs/submeshes)</description></item>
/// </list>
/// </remarks>
public partial class GeometryComponent : GameComponent
{
    static GeometryComponent()
    {
        // Register factory for creating GeometryComponent from GeometryComponentData during hydration from serialized data.
        Register<GeometryComponentData>(d =>
        {
            var g = new GeometryComponent
            {
                Name = d.Name,
                Geometry = AssetPathData.Require(d.GeometryUri, nameof(d.GeometryUri)),
            };
            g.Hydrate(d);
            return g;
        });
    }

    /// <summary>
    /// Gets or sets the geometry asset to render.
    /// </summary>
    /// <value>
    /// An absolute <c>asset:</c> URI. A geometry component always has a geometry: removing the component is how a node
    /// loses its geometry. Facts about the geometry, such as its surfaces, come from the services that own
    /// geometry assets, by this URI.
    /// </value>
    /// <exception cref="ArgumentNullException">The value is <see langword="null"/>.</exception>
    public required AssetPath Geometry
    {
        get;
        set => _ = this.SetProperty(ref field, value ?? throw new ArgumentNullException(nameof(value)));
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
            this.Geometry = AssetPathData.Require(gd.GeometryUri, nameof(gd.GeometryUri));

            // component-level override slots
            this.OverrideSlots.Clear();
            if (gd.OverrideSlots is not null)
            {
                foreach (var slotDto in gd.OverrideSlots)
                {
                    var slot = OverrideSlot.CreateAndHydrate(slotDto);
                    this.OverrideSlots.Add(slot);
                }
            }

            // targeted overrides
            this.TargetedOverrides.Clear();
            if (gd.TargetedOverrides is not null)
            {
                foreach (var targ in gd.TargetedOverrides)
                {
                    if (targ.OverrideSlots is not null)
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
        }
    }

    /// <inheritdoc/>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0305:Simplify collection initialization", Justification = "I find .ToList() is clearer with LINQ")]
    public override ComponentData Dehydrate()
        => new GeometryComponentData
        {
            Id = this.Id,
            Name = this.Name,
            GeometryUri = this.Geometry,
            OverrideSlots = this.OverrideSlots.Select(s => s.Dehydrate()).ToList(),
            TargetedOverrides = this.TargetedOverrides.Select(t => new TargetedOverrideData
            {
                LodIndex = t.LodIndex,
                SubmeshIndex = t.SubmeshIndex,
                OverrideSlots = t.OverrideSlots.Select(s => s.Dehydrate()).ToList(),
            }).ToList(),
        };
}
