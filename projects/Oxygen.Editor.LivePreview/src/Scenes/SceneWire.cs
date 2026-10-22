// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Frozen;
using System.Collections.Immutable;
using System.Globalization;
using System.Numerics;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>Where each scene property goes in the engine: its component, its fields and how its value encodes.</summary>
/// <remarks>
/// The table is generated from the engine's scene components schema, which also gives the engine its component and
/// field ids. Geometry and the scene environment have no entry: the engine takes a geometry asset with its component and
/// the environment as a whole.
/// </remarks>
internal static partial class SceneWire
{
    private static readonly FrozenDictionary<PropertyId, Mapping> Mappings = Build();

    /// <summary>Gets every property the engine takes as fields.</summary>
    public static IEnumerable<PropertyId> Properties => Mappings.Keys;

    /// <summary>Gets the engine mapping of a property.</summary>
    /// <param name="property">The property.</param>
    /// <param name="mapping">The mapping, when the engine takes the property as fields.</param>
    /// <returns><see langword="true"/> when the property has a mapping.</returns>
    public static bool TryGet(PropertyId property, out Mapping mapping) => Mappings.TryGetValue(property, out mapping!);

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

    // The schema's degrees-to-radians encoding: the inspector edits angles in degrees; the engine takes radians.
    private static ImmutableArray<float> DegreesToRadians(object? value) => [(float)value! * (MathF.PI / 180f)];

    /// <summary>A property's engine component and fields, one field per encoded value.</summary>
    /// <remarks>
    /// The engine takes every property as floats. A scalar maps to one field; a vector maps to one field per axis. The
    /// default encoding writes floats and integers as they are, booleans as one or zero and enumerations as their numeric
    /// value; <see cref="Encode"/> replaces it when the engine expects another unit.
    /// </remarks>
    /// <param name="Component">The engine component.</param>
    /// <param name="Fields">The engine fields, one per encoded value.</param>
    internal sealed record Mapping(RuntimeComponentId Component, ImmutableArray<ushort> Fields)
    {
        /// <summary>Gets the encoding of a value into one float per field, or <see langword="null"/> for the default.</summary>
        public Func<object?, ImmutableArray<float>>? Encode { get; init; }

        /// <summary>Encodes a value into one float per field.</summary>
        /// <param name="value">The property's value.</param>
        /// <returns>The encoded values, in field order.</returns>
        /// <exception cref="InvalidOperationException">The value does not encode into one float per field.</exception>
        public ImmutableArray<float> EncodeValue(object? value)
        {
            var encoded = this.Encode?.Invoke(value) ?? EncodeDefault(value);
            return encoded.Length == this.Fields.Length
                ? encoded
                : throw new InvalidOperationException($"A value of type {value?.GetType().Name ?? "null"} encodes into {encoded.Length} floats, not {this.Fields.Length}.");
        }

        private static ImmutableArray<float> EncodeDefault(object? value)
            => value switch
            {
                float scalar => [scalar],
                int integer => [integer],
                bool flag => [flag ? 1f : 0f],
                Enum enumeration => [Convert.ToInt32(enumeration, CultureInfo.InvariantCulture)],
                Vector3 vector => [vector.X, vector.Y, vector.Z],
                Vector4 vector => [vector.X, vector.Y, vector.Z, vector.W],
                null => throw new InvalidOperationException("A null value has no engine encoding."),
                _ => throw new InvalidOperationException($"A value of type {value.GetType().Name} has no default engine encoding."),
            };
    }
}
