// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Numerics;

namespace Oxygen.Editor.World;

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

    /// <summary>Gets the boolean value.</summary>
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
    public Uri? AssetUri { get; private init; }

    /// <summary>Gets the referenced node.</summary>
    public Guid NodeId { get; private init; }

    /// <summary>Creates a boolean value.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromBool(bool value) => new(ScriptValueKind.Bool) { BoolValue = value };

    /// <summary>Creates an integer value.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromInt(int value) => new(ScriptValueKind.Int32) { IntValue = value };

    /// <summary>Creates a float value.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromFloat(float value) => new(ScriptValueKind.Float) { FloatValue = value };

    /// <summary>Creates a string value.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromString(string value)
    {
        ArgumentNullException.ThrowIfNull(value);
        return new(ScriptValueKind.String) { StringValue = value };
    }

    /// <summary>Creates a two-component vector value.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromVector(Vector2 value) => new(ScriptValueKind.Vec2) { VectorValue = new(value, 0, 0) };

    /// <summary>Creates a three-component vector value.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromVector(Vector3 value) => new(ScriptValueKind.Vec3) { VectorValue = new(value, 0) };

    /// <summary>Creates a four-component vector value.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromVector(Vector4 value) => new(ScriptValueKind.Vec4) { VectorValue = value };

    /// <summary>Creates a vector value of the given kind from its components.</summary>
    /// <param name="kind"><see cref="ScriptValueKind.Vec2"/>, <see cref="ScriptValueKind.Vec3"/> or
    /// <see cref="ScriptValueKind.Vec4"/>.</param>
    /// <param name="value">The components; those past the vector size are ignored.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromVector(ScriptValueKind kind, Vector4 value)
        => kind switch
        {
            ScriptValueKind.Vec2 => FromVector(new Vector2(value.X, value.Y)),
            ScriptValueKind.Vec3 => FromVector(new Vector3(value.X, value.Y, value.Z)),
            ScriptValueKind.Vec4 => FromVector(value),
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Not a vector kind."),
        };

    /// <summary>Creates an asset reference value.</summary>
    /// <param name="asset">The asset identity.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromAsset(Uri asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        return new(ScriptValueKind.Asset) { AssetUri = asset };
    }

    /// <summary>Creates a node reference value.</summary>
    /// <param name="node">The node identity.</param>
    /// <returns>The script value.</returns>
    public static ScriptValue FromNode(Guid node)
        => node == Guid.Empty ? throw new ArgumentException("A node reference needs a node.", nameof(node)) : new(ScriptValueKind.Node) { NodeId = node };

    /// <summary>Gets the number of vector components of a kind.</summary>
    /// <param name="kind">The kind.</param>
    /// <returns>2, 3 or 4 for vectors; 0 otherwise.</returns>
    public static int VectorSize(ScriptValueKind kind)
        => kind switch
        {
            ScriptValueKind.Vec2 => 2,
            ScriptValueKind.Vec3 => 3,
            ScriptValueKind.Vec4 => 4,
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
            ScriptValueKind.Bool => this.BoolValue ? "true" : "false",
            ScriptValueKind.Int32 => this.IntValue.ToString(CultureInfo.InvariantCulture),
            ScriptValueKind.Float => this.FloatValue.ToString(CultureInfo.InvariantCulture),
            ScriptValueKind.String => this.StringValue,
            ScriptValueKind.Vec2 or ScriptValueKind.Vec3 or ScriptValueKind.Vec4
                => "(" + string.Join(", ", this.GetVectorComponents().Select(static component => component.ToString(CultureInfo.InvariantCulture))) + ")",
            ScriptValueKind.Asset => this.AssetUri?.ToString() ?? string.Empty,
            ScriptValueKind.Node => this.NodeId.ToString("D"),
            _ => string.Empty,
        };
}
