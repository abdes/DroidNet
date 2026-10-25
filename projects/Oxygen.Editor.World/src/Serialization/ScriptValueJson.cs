// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Converts script parameter values to and from their data, for script slots and for the parameter changes of an
/// instanced node alike.
/// </summary>
public static class ScriptValueJson
{
    /// <summary>Reads a script parameter value.</summary>
    /// <param name="name">The parameter's name, for the error.</param>
    /// <param name="data">The value's data.</param>
    /// <returns>The value.</returns>
    /// <exception cref="JsonException">The value is not a value of its type.</exception>
    public static ScriptValue Read(string name, ScriptValueData data)
    {
        var value = data.Value;
        try
        {
            return data.Type switch
            {
                "bool" => ScriptValue.FromBool(value.GetBoolean()),
                "int32" => ScriptValue.FromInt(value.GetInt32()),
                "float" => ScriptValue.FromFloat(value.GetSingle()),
                "string" => ScriptValue.FromString(value.GetString() ?? throw new JsonException()),
                "vec2" => ScriptValue.FromVector(ScriptValueKind.Vec2Value, ReadVector(value, 2)),
                "vec3" => ScriptValue.FromVector(ScriptValueKind.Vec3Value, ReadVector(value, 3)),
                "vec4" => ScriptValue.FromVector(ScriptValueKind.Vec4Value, ReadVector(value, 4)),
                ScriptValueData.AssetType => ScriptValue.FromAsset(AssetPath.TryParseUri(value.GetString(), out var asset) ? asset : throw new JsonException($"'{value.GetString()}' is not an asset URI.")),
                "node" => ScriptValue.FromNode(NodeReference.TryParse(value.GetString(), out var node) ? node : throw new JsonException($"'{value.GetString()}' is not a node reference.")),
                _ => throw new JsonException(),
            };
        }
        catch (Exception error) when (error is InvalidOperationException or FormatException or ArgumentException or JsonException)
        {
            throw new JsonException($"Script parameter '{name}' has an invalid {data.Type} value.", error);
        }
    }

    /// <summary>Writes a script parameter value.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The value's data.</returns>
    public static ScriptValueData Write(ScriptValue value)
        => new()
        {
            Type = ScriptingComponent.GetTypeName(value.Kind),
            Value = value.Kind switch
            {
                ScriptValueKind.BoolValue => JsonSerializer.SerializeToElement(value.BoolValue, SceneJsonContext.Default.Boolean),
                ScriptValueKind.Int32Value => JsonSerializer.SerializeToElement(value.IntValue, SceneJsonContext.Default.Int32),
                ScriptValueKind.FloatValue => JsonSerializer.SerializeToElement(value.FloatValue, SceneJsonContext.Default.Single),
                ScriptValueKind.StringValue => JsonSerializer.SerializeToElement(value.StringValue, SceneJsonContext.Default.String),
                ScriptValueKind.Vec2Value or ScriptValueKind.Vec3Value or ScriptValueKind.Vec4Value
                    => JsonSerializer.SerializeToElement(value.GetVectorComponents(), SceneJsonContext.Default.SingleArray),
                ScriptValueKind.AssetValue => JsonSerializer.SerializeToElement(value.Asset!.ToUriString(), SceneJsonContext.Default.String),
                ScriptValueKind.NodeValue => JsonSerializer.SerializeToElement(value.Node!.ToString(), SceneJsonContext.Default.String),
                _ => throw new InvalidOperationException($"Unknown script value kind {value.Kind}."),
            },
        };

    private static Vector4 ReadVector(JsonElement value, int size)
    {
        if (value.ValueKind != JsonValueKind.Array || value.GetArrayLength() != size)
        {
            throw new JsonException();
        }

        var components = new float[4];
        var index = 0;
        foreach (var component in value.EnumerateArray())
        {
            components[index++] = component.GetSingle();
        }

        return new Vector4(components[0], components[1], components[2], components[3]);
    }
}
