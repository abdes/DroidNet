// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// Component that renders a geometry asset: the engine's renderable component.
/// </summary>
/// <remarks>
/// The geometry asset supplies the defaults of its surfaces, such as each surface's material; the component stores only
/// the values this scene changes, in <see cref="Overrides"/>, by surface ID.
/// </remarks>
public partial class GeometryComponent : GameComponent, IOverridableComponent
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

    /// <inheritdoc/>
    /// <remarks>
    /// Overrides are kept when the geometry changes; those of a surface the new geometry does not have stay unresolved
    /// until they are removed.
    /// </remarks>
#pragma warning disable SA1500 // Braces for multi-line statements should not share line
#pragma warning disable SA1513 // Closing brace should be followed by blank line
    public SourceOverrides Overrides
    {
        get;
        set
        {
            ArgumentNullException.ThrowIfNull(value);
            if (!string.Equals(value.ComponentKind, SceneSchema.Geometry.Kind, StringComparison.Ordinal))
            {
                throw new ArgumentException($"The overrides of a {value.ComponentKind} component cannot be a geometry's.", nameof(value));
            }

            _ = this.SetProperty(ref field, value);
        }
    } = SourceOverrides.For(SceneSchema.Geometry.Kind);
#pragma warning restore SA1513 // Closing brace should be followed by blank line
#pragma warning restore SA1500 // Braces for multi-line statements should not share line

    /// <inheritdoc/>
    public AssetPath? PartOwner(string partKind)
        => string.Equals(partKind, SceneSchema.Geometry.Surface.Kind, StringComparison.Ordinal) ? this.Geometry : null;

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
            this.Overrides = SourceOverridesJson.Read(SceneSchema.Geometry.Kind, gd.Overrides);
        }
    }

    /// <inheritdoc/>
    public override ComponentData Dehydrate()
        => new GeometryComponentData
        {
            Id = this.Id,
            Name = this.Name,
            GeometryUri = this.Geometry,
            Overrides = SourceOverridesJson.Write(this.Overrides),
        };
}
