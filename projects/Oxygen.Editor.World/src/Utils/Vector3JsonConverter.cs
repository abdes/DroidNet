// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Utils;

/// <summary>
/// Provides JSON serialization and deserialization for <see cref="Vector3"/> objects.
/// </summary>
public sealed class Vector3JsonConverter : JsonConverter<Vector3>
{
    /// <inheritdoc/>
    public override Vector3 Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        if (reader.TokenType != JsonTokenType.StartObject)
        {
            throw new JsonException("Expected StartObject for Vector3");
        }

        float x = 0, y = 0, z = 0;

        while (reader.Read())
        {
            if (reader.TokenType == JsonTokenType.EndObject)
            {
                return new Vector3(x, y, z);
            }

            if (reader.TokenType != JsonTokenType.PropertyName)
            {
                continue;
            }

            var propName = reader.GetString();
            reader.Read();

            switch (propName)
            {
                case "x":
                case "X":
                    x = reader.GetSingle();
                    break;
                case "y":
                case "Y":
                    y = reader.GetSingle();
                    break;
                case "z":
                case "Z":
                    z = reader.GetSingle();
                    break;
                default:
                    reader.Skip();
                    break;
            }
        }

        throw new JsonException("Unexpected end when reading Vector3");
    }

    /// <inheritdoc/>
    public override void Write(Utf8JsonWriter writer, Vector3 value, JsonSerializerOptions options)
    {
        writer.WriteStartObject();
        writer.WriteNumber("X", value.X);
        writer.WriteNumber("Y", value.Y);
        writer.WriteNumber("Z", value.Z);
        writer.WriteEndObject();
    }
}
