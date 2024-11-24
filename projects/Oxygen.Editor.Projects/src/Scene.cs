// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.Projects;

/// <summary>
/// Represents a scene in a game project.
/// </summary>
/// <param name="project">The owner <see cref="Project" />.</param>
/// <remarks>
/// The <see cref="Scene"/> class represents a scene within a game project. It includes properties for the project that owns the scene and the entities within the scene. The class also provides methods for JSON serialization and deserialization.
/// </remarks>
public partial class Scene(IProject project) : GameObject
{
    /// <summary>Default template for the JsonSerializer options.</summary>
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        AllowTrailingCommas = true,
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    /// <summary>
    /// Gets the project that owns the scene.
    /// </summary>
    [JsonIgnore]
    public IProject Project { get; init; } = project;

    /// <summary>
    /// Gets the list of entities within the scene.
    /// </summary>
    public IList<GameEntity> Entities { get; init; } = [];

    /// <summary>
    /// Deserializes a JSON string into a <see cref="Scene" /> object.
    /// </summary>
    /// <param name="json">The JSON string to deserialize.</param>
    /// <param name="project">The project to set in the deserialized <see cref="Scene" /> object.</param>
    /// <returns>The deserialized <see cref="Scene" /> object.</returns>
    /// <remarks>
    /// This method uses the default <see cref="JsonSerializerOptions"/> defined in <see cref="JsonOptions"/>.
    /// </remarks>
    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Performance",
        "CA1869:Cache and reuse 'JsonSerializerOptions' instances",
        Justification = "we need to set the scene for the converter")]
    internal static Scene? FromJson(string json, IProject project)
    {
        var options = new JsonSerializerOptions(JsonOptions) { Converters = { new SceneJsonConverter(project) } };
        return JsonSerializer.Deserialize<Scene>(json, options);
    }

    /// <summary>
    /// Serializes a <see cref="Scene" /> object into a JSON string.
    /// </summary>
    /// <param name="scene">The <see cref="Scene" /> object to serialize.</param>
    /// <returns>The JSON string representation of the <see cref="Scene" /> object.</returns>
    /// <remarks>
    /// This method uses the default <see cref="JsonSerializerOptions"/> defined in <see cref="JsonOptions"/>.
    /// </remarks>
    internal static string ToJson(Scene scene) => JsonSerializer.Serialize(scene, JsonOptions);
}
