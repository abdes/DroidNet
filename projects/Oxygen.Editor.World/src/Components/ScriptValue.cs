// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Numerics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// A typed script parameter value set on a slot.
/// </summary>
/// <remarks>
/// Vectors keep their components in <see cref="VectorValue"/>, unused components zero. An asset value is the asset's
/// identity URI; a node value is the identity of a node in the same scene, converted to a node index when cooked.
/// </remarks>
public sealed record ScriptValue
{
    private ScriptValue(ScriptValueKind kind)
    {
        this.Kind = kind;
    }

    /// <summary>Gets the value type.</summary>
    public ScriptValueKind Kind { get; }

    /// <summary>Gets a value indicating whether the boolean value is true.</summary>
    public bool BoolValue { get; private init; }

    /// <summary>Gets the integer value.</summary>
    public int IntValue { get; private init; }

    /// <summary>Gets the float value.</summary>
    public float FloatValue { get; private init; }

    /// <summary>Gets the string value.</summary>
    public string StringValue { get; private init; } = string.Empty;

    /// <summary>Gets the vector value; components past the vector size are zero.</summary>
    public Vector4 VectorValue { get; private init; }

    /// <summary>Gets the referenced asset.</summary>
    public AssetPath? Asset { get; private init; }

    /// <summary>Gets the referenced node: a node of the scene or one an instance brings in; set for a node value.</summary>
    public NodeReference? Node { get; private init; }

    /// <summary>Creates a boolean value.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromBool(bool value) => new(ScriptValueKind.BoolValue) { BoolValue = value };

    /// <summary>Creates an integer value.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromInt(int value) => new(ScriptValueKind.Int32Value) { IntValue = value };

    /// <summary>Creates a float value.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromFloat(float value) => new(ScriptValueKind.FloatValue) { FloatValue = value };

    /// <summary>Creates a string value.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromString(string value)
    {
        ArgumentNullException.ThrowIfNull(value);
        return new(ScriptValueKind.StringValue) { StringValue = value };
    }

    /// <summary>Creates a two-component vector value.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromVector(Vector2 value) => new(ScriptValueKind.Vec2Value) { VectorValue = new(value, 0, 0) };

    /// <summary>Creates a three-component vector value.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromVector(Vector3 value) => new(ScriptValueKind.Vec3Value) { VectorValue = new(value, 0) };

    /// <summary>Creates a four-component vector value.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromVector(Vector4 value) => new(ScriptValueKind.Vec4Value) { VectorValue = value };

    /// <summary>Creates a vector value of the given kind from its components.</summary>
    /// <param name="kind"><see cref="ScriptValueKind.Vec2Value"/>, <see cref="ScriptValueKind.Vec3Value"/> or
    /// <see cref="ScriptValueKind.Vec4Value"/>.</param>
    /// <param name="value">The components; those past the vector size are ignored.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromVector(ScriptValueKind kind, Vector4 value)
        => kind switch
        {
            ScriptValueKind.Vec2Value => FromVector(new Vector2(value.X, value.Y)),
            ScriptValueKind.Vec3Value => FromVector(new Vector3(value.X, value.Y, value.Z)),
            ScriptValueKind.Vec4Value => FromVector(value),
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Not a vector kind."),
        };

    /// <summary>Creates an asset reference value.</summary>
    /// <param name="asset">The asset identity.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromAsset(AssetPath asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        return new(ScriptValueKind.AssetValue) { Asset = asset };
    }

    /// <summary>Creates a node reference value of a node of the scene.</summary>
    /// <param name="node">The node identity.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromNode(NodeId node)
        => node.IsEmpty ? throw new ArgumentException("A node reference needs a node.", nameof(node)) : FromNode(NodeReference.To(node));

    /// <summary>Creates a node reference value.</summary>
    /// <param name="node">The node: a node of the scene or one an instance brings in.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromNode(NodeReference node)
    {
        ArgumentNullException.ThrowIfNull(node);
        return new(ScriptValueKind.NodeValue) { Node = node };
    }

    /// <summary>Gets the number of vector components of a kind.</summary>
    /// <param name="kind">The kind.</param>
    /// <returns>2, 3 or 4 for vectors; 0 otherwise.</returns>
    public static int VectorSize(ScriptValueKind kind)
        => kind switch
        {
            ScriptValueKind.Vec2Value => 2,
            ScriptValueKind.Vec3Value => 3,
            ScriptValueKind.Vec4Value => 4,
            _ => 0,
        };

    /// <summary>Gets the vector components as an array of the vector's size.</summary>
    /// <returns>The components; empty when the value is not a vector.</returns>
    public float[] GetVectorComponents()
        => [.. new[] { this.VectorValue.X, this.VectorValue.Y, this.VectorValue.Z, this.VectorValue.W }.Take(VectorSize(this.Kind))];

    /// <inheritdoc />
    public override string ToString()
        => this.Kind switch
        {
            ScriptValueKind.BoolValue => this.BoolValue ? "true" : "false",
            ScriptValueKind.Int32Value => this.IntValue.ToString(CultureInfo.InvariantCulture),
            ScriptValueKind.FloatValue => this.FloatValue.ToString(CultureInfo.InvariantCulture),
            ScriptValueKind.StringValue => this.StringValue,
            ScriptValueKind.Vec2Value or ScriptValueKind.Vec3Value or ScriptValueKind.Vec4Value
                => "(" + string.Join(", ", this.GetVectorComponents().Select(static component => component.ToString(CultureInfo.InvariantCulture))) + ")",
            ScriptValueKind.AssetValue => this.Asset?.ToUriString() ?? string.Empty,
            ScriptValueKind.NodeValue => this.Node?.ToString() ?? string.Empty,
            _ => string.Empty,
        };
}
