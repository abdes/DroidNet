// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.World;

/// <summary>
///     Represents a component of a scene node, such as transform, geometry, material, etc.
/// </summary>
/// <param name="node">The owner <see cref="SceneNode" />.</param>
[JsonDerivedType(typeof(Transform), "Transform")]
[JsonDerivedType(typeof(GameComponent), "Base")]
public partial class GameComponent(SceneNode node) : GameObject
{
    /// <summary>
    ///     JsonSerializer options for <see cref="GameComponent" /> object, internally visible to be
    ///     available when a <see cref="SceneNode" /> is manually serializing its components.
    /// </summary>
    internal static readonly JsonSerializerOptions JsonOptions = new()
    {
        AllowTrailingCommas = true,
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        Converters = { new Vector3JsonConverter() },
    };

    /// <summary>
    ///     Gets the owner scene node of this component.
    /// </summary>
    /// <remarks>
    ///     Allow the owning assembly (SceneNode converter) to set the Node after
    ///     deserialization.
    /// </remarks>
    [JsonIgnore]
    public SceneNode Node { get; internal set; } = node;

    /// <summary>
    ///     Deserializes a JSON string into a <see cref="GameComponent" /> object.
    /// </summary>
    /// <param name="json">The JSON string to deserialize.</param>
    /// <returns>The deserialized <see cref="GameComponent" /> object.</returns>
    /// <remarks>
    ///     This method uses the default <see cref="JsonSerializerOptions" /> defined in <see cref="JsonOptions" />.
    /// </remarks>
    public static GameComponent? FromJson(string json) => JsonSerializer.Deserialize<GameComponent>(json, JsonOptions);

    /// <summary>
    ///     Serializes a <see cref="GameComponent" /> object into a JSON string.
    /// </summary>
    /// <param name="gameComponent">The <see cref="GameComponent" /> object to serialize.</param>
    /// <returns>The JSON string representation of the <see cref="GameComponent" /> object.</returns>
    /// <remarks>
    ///     This method uses the default <see cref="JsonSerializerOptions" /> defined in <see cref="JsonOptions" />.
    /// </remarks>
    public static string ToJson(GameComponent gameComponent) => JsonSerializer.Serialize(gameComponent, JsonOptions);

    /// <summary>
    ///     A custom JSON converter for <see cref="Vector3" />.
    /// </summary>
    private sealed class Vector3JsonConverter : JsonConverter<Vector3>
    {
        /// <inheritdoc />
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
                    _ = reader.Read();
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
                            _ = 0; // Ignore unknown properties
                            break;
                    }
                }
            }

            Fail.MalformedObject(typeof(Vector3).FullName!);

            // Never reached
            Debug.Fail("Unreachable code executed");
            return default;
        }

        /// <inheritdoc />
        public override void Write(Utf8JsonWriter writer, Vector3 value, JsonSerializerOptions options)
        {
            writer.WriteStartObject();
            writer.WriteNumber("x", value.X);
            writer.WriteNumber("y", value.Y);
            writer.WriteNumber("z", value.Z);
            writer.WriteEndObject();
        }
    }

    /// <summary>
    ///     Provides helper methods for throwing JSON-related exceptions during serialization and deserialization.
    /// </summary>
    private abstract class Fail : JsonThrowHelper<SceneNode>
    {
        /// <summary>
        ///     Throws a <see cref="JsonException" /> indicating that a malformed object was encountered.
        /// </summary>
        /// <param name="objectType">The type of the malformed object.</param>
        /// <exception cref="JsonException">Always thrown to indicate the malformed object.</exception>
        [DoesNotReturn]
        public static void MalformedObject(string objectType)
            => throw new JsonException(FormatErrorMessage($"encountered a malformed object of type: {objectType}"));
    }
}
