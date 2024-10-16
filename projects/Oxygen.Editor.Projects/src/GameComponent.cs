// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Oxygen.Editor.Projects.Utils;

/// <summary>
/// Represents a component of a game entity, such as transform, geometry, material, etc.
/// </summary>
/// <param name="entity">The owner <see cref="GameEntity" />.</param>
[JsonDerivedType(typeof(Transform), typeDiscriminator: "Transform")]
[JsonDerivedType(typeof(GameComponent), typeDiscriminator: "Base")]
public partial class GameComponent(GameEntity entity) : GameObject
{
    /// <summary>Default template for the JsonSerializer options.</summary>
    public static readonly JsonSerializerOptions JsonOptions = new()
    {
        AllowTrailingCommas = true,
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        Converters = { new Vector3JsonConverter() },
    };

    [JsonIgnore]
    public GameEntity Entity { get; } = entity;

    /// <summary>
    /// Deserializes a JSON string into a <see cref="GameComponent" /> object.
    /// </summary>
    /// <param name="json">The JSON string to deserialize.</param>
    /// <returns>The deserialized <see cref="GameEntity" /> object.</returns>
    public static GameComponent? FromJson(string json)
        => JsonSerializer.Deserialize<GameComponent>(json, JsonOptions);

    /// <summary>
    /// Serializes a <see cref="GameComponent" /> object into a JSON string.
    /// </summary>
    /// <param name="gameComponent">The <see cref="GameComponent" /> object to serialize.</param>
    /// <returns>The JSON string representation of the <see cref="GameEntity" /> object.</returns>
    public static string ToJson(GameComponent gameComponent) => JsonSerializer.Serialize(gameComponent, JsonOptions);

    public class Vector3JsonConverter : JsonConverter<Vector3>
    {
        public override Vector3 Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            if (reader.TokenType != JsonTokenType.StartObject)
            {
                throw new JsonException();
            }

            float x = 0, y = 0, z = 0;

            while (reader.Read())
            {
                if (reader.TokenType == JsonTokenType.EndObject)
                {
                    return new Vector3(x, y, z);
                }

                if (reader.TokenType == JsonTokenType.PropertyName)
                {
                    var propertyName = reader.GetString();
                    reader.Read();
                    switch (propertyName)
                    {
                        case "x":
                            x = reader.GetSingle();
                            break;
                        case "y":
                            y = reader.GetSingle();
                            break;
                        case "z":
                            z = reader.GetSingle();
                            break;

                        default:
                            // Ignore
                            break;
                    }
                }
            }

            Fail.MalformedObject(typeof(Vector3).FullName!);

            // Never reached
            Debug.Fail("Unreachable code executed");
            return default;
        }

        public override void Write(Utf8JsonWriter writer, Vector3 value, JsonSerializerOptions options)
        {
            writer.WriteStartObject();
            writer.WriteNumber("x", value.X);
            writer.WriteNumber("y", value.Y);
            writer.WriteNumber("z", value.Z);
            writer.WriteEndObject();
        }
    }

    private abstract class Fail : JsonThrowHelper<GameEntity>
    {
        [DoesNotReturn]
        public static void MalformedObject(string objectType)
            => throw new JsonException(FormatErrorMessage($"encountered a malformed object of type: {objectType}"));
    }
}
