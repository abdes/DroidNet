// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

using System.Text.Json;
using System.Text.Json.Serialization;
using Oxygen.Editor.Projects.Utils;

/// <summary>
/// Represents a game entity with a name and an associated scene.
/// </summary>
/// <param name="scene">The scene associated with the game entity.</param>
public class GameEntity(Scene scene) : NamedItem
{
    /// <summary>Default template for the JsonSerializer options.</summary>
    public static readonly JsonSerializerOptions JsonOptions = new()
    {
        AllowTrailingCommas = true,
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    /// <summary>
    /// Gets the scene associated with the game entity.
    /// </summary>
    [JsonIgnore]
    public Scene Scene { get; } = scene;

    /// <summary>
    /// Deserializes a JSON string into a <see cref="GameEntity" /> object.
    /// </summary>
    /// <param name="json">The JSON string to deserialize.</param>
    /// <param name="scene">The scene to set in the deserialized <see cref="GameEntity" /> object.</param>
    /// <returns>The deserialized <see cref="GameEntity" /> object.</returns>
    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Performance",
        "CA1869:Cache and reuse 'JsonSerializerOptions' instances",
        Justification = "we need to set the scene for the converter")]
    public static GameEntity? FromJson(string json, Scene scene)
    {
        var options = new JsonSerializerOptions(JsonOptions) { Converters = { new GameEntityConverter(scene) } };
        return JsonSerializer.Deserialize<GameEntity>(json, options);
    }

    /// <summary>
    /// Serializes a <see cref="GameEntity" /> object into a JSON string.
    /// </summary>
    /// <param name="gameEntity">The <see cref="GameEntity" /> object to serialize.</param>
    /// <returns>The JSON string representation of the <see cref="GameEntity" /> object.</returns>
    public static string ToJson(GameEntity gameEntity) => JsonSerializer.Serialize(gameEntity, JsonOptions);

    internal sealed class GameEntityConverter(Scene scene) : JsonConverter<GameEntity>
    {
        public override GameEntity Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            var entity = JsonSerializer.Deserialize<JsonElement>(ref reader, options);

            // ReSharper disable once ArrangeStaticMemberQualifier
            if (!entity.TryGetProperty(nameof(NamedItem.Name), out var nameElement))
            {
                // ReSharper disable once ArrangeStaticMemberQualifier
                Fail.MissingRequiredProperty(nameof(NamedItem.Name));
            }

            var name = nameElement.ToString();

            return new GameEntity(scene) { Name = name };
        }

        public override void Write(Utf8JsonWriter writer, GameEntity value, JsonSerializerOptions options)
        {
            writer.WriteStartObject();

            // ReSharper disable once ArrangeStaticMemberQualifier
            writer.WriteString(nameof(NamedItem.Name), value.Name);

            // Omit the Scene property
            writer.WriteEndObject();
        }

        private abstract class Fail : JsonThrowHelper<GameEntity>
        {
            public static new void MissingRequiredProperty(string propertyName)
                => JsonThrowHelper<GameEntity>.MissingRequiredProperty(propertyName);
        }
    }
}
