// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Frozen;
using System.Collections.Immutable;
using System.Globalization;
using System.Numerics;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>Where each scene property goes in the engine: its component, its fields and how its value encodes.</summary>
/// <remarks>
/// The table is generated from the engine's scene components schema, which also gives the engine its component and
/// field ids. Geometry and the scene environment have no field entry: the engine takes a geometry asset with its component
/// and the environment as a whole. A property a source supplies goes as an override, by its override id.
/// </remarks>
internal static partial class SceneWire
{
    private static readonly FrozenDictionary<PropertyId, Mapping> Mappings = Build();

    private static readonly FrozenDictionary<PropertyId, RuntimeOverrideId> OverrideIds = BuildOverrideIds();

    private static readonly FrozenDictionary<PropertyId, RuntimeOverrideId> ChangeIds = BuildChangeIds();

    /// <summary>Gets every property the engine takes as fields.</summary>
    public static IEnumerable<PropertyId> Properties => Mappings.Keys;

    /// <summary>Gets the engine mapping of a property.</summary>
    /// <param name="property">The property.</param>
    /// <param name="mapping">The mapping, when the engine takes the property as fields.</param>
    /// <returns><see langword="true"/> when the property has a mapping.</returns>
    public static bool TryGet(PropertyId property, out Mapping mapping) => Mappings.TryGetValue(property, out mapping!);

    /// <summary>Gets the override id of a property the engine takes as an override.</summary>
    /// <param name="property">The property.</param>
    /// <param name="id">The override id, when the property is one.</param>
    /// <returns><see langword="true"/> when the engine takes the property as an override.</returns>
    public static bool TryGetOverrideId(PropertyId property, out RuntimeOverrideId id) => OverrideIds.TryGetValue(property, out id);

    /// <summary>Gets the override id of a property a scene can change in an instance.</summary>
    /// <param name="property">The property.</param>
    /// <param name="id">The override id, when the property is changeable.</param>
    /// <returns><see langword="true"/> when an instance change can set the property.</returns>
    public static bool TryGetChangeId(PropertyId property, out RuntimeOverrideId id) => ChangeIds.TryGetValue(property, out id);

    /// <summary>Gets a value indicating whether a node has the component that an engine component id addresses.</summary>
    /// <param name="node">The node.</param>
    /// <param name="component">The engine component.</param>
    /// <returns><see langword="true"/> when the node has the component.</returns>
    public static bool HasComponent(SceneNode node, RuntimeComponentId component)
    {
        ArgumentNullException.ThrowIfNull(node);
        return component switch
        {
            RuntimeComponentId.Transform => node.Components.Any(static value => value is TransformComponent),
            RuntimeComponentId.PerspectiveCamera => node.Components.Any(static value => value is PerspectiveCamera),
            RuntimeComponentId.OrthographicCamera => node.Components.Any(static value => value is OrthographicCamera),
            RuntimeComponentId.DirectionalLight => node.Components.Any(static value => value is DirectionalLightComponent),
            RuntimeComponentId.PointLight => node.Components.Any(static value => value is PointLightComponent),
            RuntimeComponentId.SpotLight => node.Components.Any(static value => value is SpotLightComponent),
            RuntimeComponentId.LocalFogVolume => node.Components.Any(static value => value is LocalFogVolumeComponent),
            RuntimeComponentId.Node => true,
            _ => false,
        };
    }

    private static FrozenDictionary<PropertyId, Mapping> Build()
    {
        var map = new Dictionary<PropertyId, Mapping>();
        AddSchemaMappings(map);
        return map.ToFrozenDictionary();
    }

    private static FrozenDictionary<PropertyId, RuntimeOverrideId> BuildOverrideIds()
    {
        var map = new Dictionary<PropertyId, RuntimeOverrideId>();
        AddSchemaOverrideIds(map);
        return map.ToFrozenDictionary();
    }

    private static FrozenDictionary<PropertyId, RuntimeOverrideId> BuildChangeIds()
    {
        var map = new Dictionary<PropertyId, RuntimeOverrideId>();
        AddSchemaChangeIds(map);
        return map.ToFrozenDictionary();
    }

    // The schema's degrees-to-radians encoding: the inspector edits angles in degrees; the engine takes radians.
    private static ImmutableArray<RuntimeFieldValue> DegreesToRadians(object? value) => [new RuntimeFieldValue.Scalar((float)value! * (MathF.PI / 180f))];

    /// <summary>A property's engine component and fields, one field per encoded value.</summary>
    /// <remarks>
    /// Each field has one schema value type, which the engine checks. A scalar maps to one field; a vector maps to one
    /// float field per axis. The default encoding writes a float as a <see cref="RuntimeFieldValue.Scalar"/>, an integer
    /// or an enumeration's numeric value as a <see cref="RuntimeFieldValue.Integer"/> and a boolean as a
    /// <see cref="RuntimeFieldValue.Flag"/>; <see cref="Encode"/> replaces it when the engine expects another unit.
    /// </remarks>
    /// <param name="Component">The engine component.</param>
    /// <param name="Fields">The engine fields, one per encoded value.</param>
    internal sealed record Mapping(RuntimeComponentId Component, ImmutableArray<ushort> Fields)
    {
        /// <summary>Gets the encoding of a value into one value per field, or <see langword="null"/> for the default.</summary>
        public Func<object?, ImmutableArray<RuntimeFieldValue>>? Encode { get; init; }

        /// <summary>Encodes a value into one value per field.</summary>
        /// <param name="value">The property's value.</param>
        /// <returns>The encoded values, in field order.</returns>
        /// <exception cref="InvalidOperationException">The value does not encode into one value per field.</exception>
        public ImmutableArray<RuntimeFieldValue> EncodeValue(object? value)
        {
            var encoded = this.Encode?.Invoke(value) ?? EncodeDefault(value);
            return encoded.Length == this.Fields.Length
                ? encoded
                : throw new InvalidOperationException($"A value of type {value?.GetType().Name ?? "null"} encodes into {encoded.Length} values, not {this.Fields.Length}.");
        }

        private static ImmutableArray<RuntimeFieldValue> EncodeDefault(object? value)
            => value switch
            {
                float scalar => [new RuntimeFieldValue.Scalar(scalar)],
                int integer => [new RuntimeFieldValue.Integer(integer)],
                bool flag => [new RuntimeFieldValue.Flag(flag)],
                Enum enumeration => [new RuntimeFieldValue.Integer(Convert.ToInt32(enumeration, CultureInfo.InvariantCulture))],
                Vector3 vector => [new RuntimeFieldValue.Scalar(vector.X), new RuntimeFieldValue.Scalar(vector.Y), new RuntimeFieldValue.Scalar(vector.Z)],
                null => throw new InvalidOperationException("A null value has no engine encoding."),
                _ => throw new InvalidOperationException($"A value of type {value.GetType().Name} has no default engine encoding."),
            };
    }
}
