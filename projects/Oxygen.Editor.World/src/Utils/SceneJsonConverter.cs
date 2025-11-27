// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Utils;

/// <summary>
///     A custom JSON converter for the <see cref="Scene" /> class.
/// </summary>
/// <param name="project">The project associated with the scene being serialized or deserialized.</param>
/// <remarks>
///     The <see cref="SceneJsonConverter" /> class provides custom serialization and deserialization logic for the
///     <see cref="Scene" /> class.
///     It ensures that the <see cref="Scene" /> object is correctly serialized and deserialized, including its properties
///     and nested scene nodes.
/// </remarks>
internal class SceneJsonConverter(IProject project) : JsonConverter<Scene>
{
    /// <inheritdoc />
    public override Scene Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        var sceneElement = JsonSerializer.Deserialize<JsonElement>(ref reader, options);

        if (!sceneElement.TryGetProperty(nameof(GameObject.Name), out var nameElement))
        {
            Fail.MissingRequiredProperty(nameof(GameObject.Name));
        }

        var name = nameElement.ToString();

        var id = sceneElement.TryGetProperty(nameof(GameObject.Id), out var idElement) &&
                 idElement.TryGetGuid(out var parsedId)
            ? parsedId
            : Guid.NewGuid();

        var scene = new Scene(project) { Name = name, Id = id };

        if (!sceneElement.TryGetProperty(nameof(Scene.Nodes), out var entitiesElement) ||
            entitiesElement.ValueKind != JsonValueKind.Array)
        {
            return scene;
        }

        scene.Nodes.Clear();
        foreach (var nodeElement in entitiesElement.EnumerateArray())
        {
            var sceneNode = SceneNode.FromJson(nodeElement.GetRawText(), scene);
            if (sceneNode != null)
            {
                scene.Nodes.Add(sceneNode);
            }
        }

        return scene;
    }

    /// <inheritdoc />
    public override void Write(Utf8JsonWriter writer, Scene value, JsonSerializerOptions options)
    {
        writer.WriteStartObject();

        writer.WriteString(nameof(GameObject.Name), value.Name);
        writer.WriteString(nameof(GameObject.Id), value.Id);

        writer.WritePropertyName(nameof(Scene.Nodes));
        JsonSerializer.Serialize(writer, value.Nodes, options);

        writer.WriteEndObject();
    }

    /// <summary>
    ///     Provides helper methods for throwing JSON-related exceptions during serialization and deserialization.
    /// </summary>
    private abstract class Fail : JsonThrowHelper<SceneNode>
    {
        /// <summary>
        ///     Throws a <see cref="JsonException" /> indicating that a required property is missing.
        /// </summary>
        /// <param name="propertyName">The name of the missing property.</param>
        /// <exception cref="JsonException">Always thrown to indicate the missing property.</exception>
        public static new void MissingRequiredProperty(string propertyName)
            => JsonThrowHelper<SceneNode>.MissingRequiredProperty(propertyName);
    }
}
