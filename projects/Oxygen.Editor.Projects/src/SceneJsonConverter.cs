// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;
using Oxygen.Editor.Projects.Utils;

namespace Oxygen.Editor.Projects;

/// <summary>
/// A custom JSON converter for the <see cref="Scene"/> class.
/// </summary>
/// <param name="project">The project associated with the scene being serialized or deserialized.</param>
/// <remarks>
/// The <see cref="SceneJsonConverter"/> class provides custom serialization and deserialization logic for the <see cref="Scene"/> class.
/// It ensures that the <see cref="Scene"/> object is correctly serialized and deserialized, including its properties and nested entities.
/// </remarks>
internal class SceneJsonConverter(IProject project) : JsonConverter<Scene>
{
    /// <inheritdoc/>
    public override Scene Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        var scene = JsonSerializer.Deserialize<JsonElement>(ref reader, options);

        if (!scene.TryGetProperty(nameof(GameObject.Name), out var nameElement))
        {
            Fail.MissingRequiredProperty(nameof(GameObject.Name));
        }

        var name = nameElement.ToString();

        var entities = new List<GameEntity>();

        if (scene.TryGetProperty(nameof(Scene.Entities), out var entitiesElement) &&
            entitiesElement.ValueKind == JsonValueKind.Array)
        {
            entities.AddRange(
                entitiesElement.EnumerateArray()
                    .Select(entityElement => JsonSerializer.Deserialize<GameEntity>(entityElement.GetRawText()))
                    .OfType<GameEntity>());
        }

        return new Scene(project)
        {
            Name = name,
            Entities = entities,
        };
    }

    /// <inheritdoc/>
    public override void Write(Utf8JsonWriter writer, Scene value, JsonSerializerOptions options)
    {
        writer.WriteStartObject();

        writer.WriteString(nameof(GameObject.Name), value.Name);

        writer.WritePropertyName(nameof(Scene.Entities));
        JsonSerializer.Serialize(writer, value.Entities, options);

        writer.WriteEndObject();
    }

    /// <summary>
    /// Provides helper methods for throwing JSON-related exceptions during serialization and deserialization.
    /// </summary>
    private abstract class Fail : JsonThrowHelper<GameEntity>
    {
        /// <summary>
        /// Throws a <see cref="JsonException"/> indicating that a required property is missing.
        /// </summary>
        /// <param name="propertyName">The name of the missing property.</param>
        /// <exception cref="JsonException">Always thrown to indicate the missing property.</exception>
        public static new void MissingRequiredProperty(string propertyName)
            => JsonThrowHelper<GameEntity>.MissingRequiredProperty(propertyName);
    }
}
