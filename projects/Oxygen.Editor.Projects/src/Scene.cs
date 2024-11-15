// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

using System.Text.Json;
using System.Text.Json.Serialization;
using Oxygen.Editor.Projects.Utils;

/// <summary>
/// Represents a scene in a game project.
/// </summary>
/// <param name="project">The owner <see cref="Project" />.</param>
public partial class Scene(IProject project) : GameObject
{
    /// <summary>Default template for the JsonSerializer options.</summary>
    public static readonly JsonSerializerOptions JsonOptions = new()
    {
        AllowTrailingCommas = true,
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    [JsonIgnore]
    public IProject Project { get; } = project;

    public IList<GameEntity> Entities { get; internal init; } = [];

    /// <summary>
    /// Deserializes a JSON string into a <see cref="Scene" /> object.
    /// </summary>
    /// <param name="json">The JSON string to deserialize.</param>
    /// <param name="project">The project to set in the deserialized <see cref="Scene" /> object.</param>
    /// <returns>The deserialized <see cref="Scene" /> object.</returns>
    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Performance",
        "CA1869:Cache and reuse 'JsonSerializerOptions' instances",
        Justification = "we need to set the scene for the converter")]
    public static Scene? FromJson(string json, IProject project)
    {
        var options = new JsonSerializerOptions(JsonOptions) { Converters = { new SceneJsonConverter(project) } };
        return JsonSerializer.Deserialize<Scene>(json, options);
    }

    /// <summary>
    /// Serializes a <see cref="Scene" /> object into a JSON string.
    /// </summary>
    /// <param name="scene">The <see cref="Scene" /> object to serialize.</param>
    /// <returns>The JSON string representation of the <see cref="Scene" /> object.</returns>
    public static string ToJson(Scene scene) => JsonSerializer.Serialize(scene, JsonOptions);
}

public class SceneJsonConverter(IProject project) : JsonConverter<Scene>
{
    public override Scene Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        var scene = JsonSerializer.Deserialize<JsonElement>(ref reader, options);

        // ReSharper disable once ArrangeStaticMemberQualifier
        if (!scene.TryGetProperty(nameof(GameObject.Name), out var nameElement))
        {
            // ReSharper disable once ArrangeStaticMemberQualifier
            Fail.MissingRequiredProperty(nameof(GameObject.Name));
        }

        var name = nameElement.ToString();

        var entities = new List<GameEntity>();

        // ReSharper disable once ArrangeStaticMemberQualifier
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

    public override void Write(Utf8JsonWriter writer, Scene value, JsonSerializerOptions options)
    {
        writer.WriteStartObject();

        // ReSharper disable once ArrangeStaticMemberQualifier
        writer.WriteString(nameof(GameObject.Name), value.Name);

        // ReSharper disable once ArrangeStaticMemberQualifier
        writer.WritePropertyName(nameof(Scene.Entities));
        JsonSerializer.Serialize(writer, value.Entities, options);

        // Omit the Scene property
        writer.WriteEndObject();
    }

    private abstract class Fail : JsonThrowHelper<GameEntity>
    {
        public static new void MissingRequiredProperty(string propertyName)
            => JsonThrowHelper<GameEntity>.MissingRequiredProperty(propertyName);
    }
}
