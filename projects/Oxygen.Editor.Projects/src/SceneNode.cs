// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using System.Text.Json.Serialization;
using Oxygen.Editor.Projects.Utils;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Represents a scene node with a name and an associated scene.
/// </summary>
/// <remarks>
///     The <see cref="SceneNode" /> class represents a scene node within a game, which is associated with a specific
///     scene.
///     It provides methods for JSON serialization and deserialization, allowing scene nodes to be easily saved and
///     loaded.
/// </remarks>
public partial class SceneNode : GameObject, IDisposable
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        AllowTrailingCommas = true,
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    private bool isActive;
    private bool isDisposed;

    // private bool isDisposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneNode" /> class.
    /// </summary>
    /// <param name="scene">The scene associated with the scene node.</param>
    public SceneNode(Scene scene)
    {
        this.Scene = scene;

        // Initialize the components collection with the always-present Transform.
        // Use a concrete mutable collection that preserves insertion order.
        this.Components = new List<GameComponent> { new Transform(this) { Name = nameof(Transform) } };
    }

    /// <summary>
    ///     Gets or sets a value indicating whether the scene node is loaded in the Game Engine.
    /// </summary>
    public bool IsActive
    {
        get => this.isActive;
        set
        {
            if (this.isActive == value)
            {
                return;
            }

            _ = this.SetField(ref this.isActive, value);
        }
    }

    /// <summary>
    ///     Gets the scene associated with the scene node.
    /// </summary>
    [JsonIgnore]
    public Scene Scene { get; }

    /// <summary>
    ///     Gets the list of components associated with the scene node.
    /// </summary>
    public ICollection<GameComponent> Components { get; private init; }

    /// <inheritdoc />
    public void Dispose()
    {
        // Do not change this code. Put cleanup code in 'Dispose(bool disposing)' method
        this.Dispose(true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    ///     Deserializes a JSON string into a <see cref="SceneNode" /> object.
    /// </summary>
    /// <param name="json">The JSON string to deserialize.</param>
    /// <param name="scene">The scene to set in the deserialized <see cref="SceneNode" /> object.</param>
    /// <returns>The deserialized <see cref="SceneNode" /> object.</returns>
    /// <remarks>
    ///     This method uses the default <see cref="JsonSerializerOptions" /> defined in <see cref="JsonOptions" />.
    /// </remarks>
    [SuppressMessage(
        "Performance",
        "CA1869:Cache and reuse 'JsonSerializerOptions' instances",
        Justification = "we need to set the scene for the converter")]
    internal static SceneNode? FromJson(string json, Scene scene)
    {
        var options = new JsonSerializerOptions(JsonOptions) { Converters = { new SceneNodeConverter(scene) } };
        return JsonSerializer.Deserialize<SceneNode>(json, options);
    }

    /// <summary>
    ///     Serializes a <see cref="SceneNode" /> object into a JSON string.
    /// </summary>
    /// <param name="sceneNode">The <see cref="SceneNode" /> object to serialize.</param>
    /// <returns>The JSON string representation of the <see cref="SceneNode" /> object.</returns>
    /// <remarks>
    ///     This method uses the default <see cref="JsonSerializerOptions" /> defined in <see cref="JsonOptions" />.
    /// </remarks>
    [SuppressMessage(
        "Performance",
        "CA1869:Cache and reuse 'JsonSerializerOptions' instances",
        Justification = "we need to use the custom converter")]
    internal static string ToJson(SceneNode sceneNode)
    {
        var options = new JsonSerializerOptions(JsonOptions) { Converters = { new SceneNodeConverter(null!) } };
        return JsonSerializer.Serialize(sceneNode, options);
    }

    /// <summary>
    ///     Disposes of the scene node.
    /// </summary>
    /// <param name="disposing">A value indicating whether the scene node is being disposed of deterministically.</param>
    /// <remarks>
    ///     The disposing parameter should be false when called from a finalizer, and true when called
    ///     from the IDisposable.Dispose method. In other words, it is true when deterministically
    ///     called and false when non-deterministically called.
    /// </remarks>
    protected virtual void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
        }

        // TODO: free unmanaged resources (unmanaged objects) and override finalizer
        // TODO: set large fields to null
        this.isDisposed = true;
    }

    /// <summary>
    ///     A custom JSON converter for <see cref="SceneNode" /> because we want to enforce that a
    ///     <c>SceneNode</c> can only be created with the <see cref="Scene" /> to which it belongs.
    /// </summary>
    internal sealed class SceneNodeConverter(Scene scene) : JsonConverter<SceneNode>
    {
        /// <inheritdoc />
        public override SceneNode Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            var nodeElement = JsonSerializer.Deserialize<JsonElement>(ref reader, options);

            if (!nodeElement.TryGetProperty(nameof(SceneNode.Name), out var nameElement))
            {
                Fail.MissingRequiredProperty(nameof(SceneNode.Name));
            }

            var name = nameElement.ToString();

            var isActive = nodeElement.TryGetProperty(nameof(SceneNode.IsActive), out var isActiveElement) &&
                           isActiveElement.GetBoolean();
            var sceneNode = new SceneNode(scene) { Name = name };

            if (nodeElement.TryGetProperty(nameof(SceneNode.Components), out var elComponents) &&
                elComponents.ValueKind == JsonValueKind.Array)
            {
                // Clear any constructor-injected components before populating from JSON
                sceneNode.Components.Clear();
                foreach (var elComponent in elComponents.EnumerateArray())
                {
                    var component = GameComponent.FromJson(elComponent.GetRawText());
                    if (component != null)
                    {
                        // Ensure the component's Node points to the deserialized node
                        component.Node = sceneNode;
                        sceneNode.Components.Add(component);
                    }
                }
            }

            // Ensure the Components list contains a Transform element
            if (!sceneNode.Components.OfType<Transform>().Any())
            {
                sceneNode.Components.Add(new Transform(sceneNode) { Name = nameof(Transform) });
            }

            // Finally set the scene node's active state
            sceneNode.IsActive = isActive;

            return sceneNode;
        }

        /// <inheritdoc />
        public override void Write(Utf8JsonWriter writer, SceneNode value, JsonSerializerOptions options)
        {
            writer.WriteStartObject();

            writer.WriteString(nameof(SceneNode.Name), value.Name);
            writer.WriteBoolean(nameof(SceneNode.IsActive), value.IsActive);

            writer.WritePropertyName(nameof(SceneNode.Components));
            var componentSerializerOptions = new JsonSerializerOptions(options);
            foreach (var converter in GameComponent.JsonOptions.Converters)
            {
                componentSerializerOptions.Converters.Add(converter);
            }

            // When serializing, place user-provided components before the injected
            // default Transform (which is present in the collection by constructor).
            // This ensures the JSON lists the meaningful components first so that
            // deserialization produces a collection where those components appear
            // in the expected order.
            var ordered = value.Components
                .OrderBy(c =>
                    c is Transform && string.Equals(c.Name, nameof(Transform), StringComparison.Ordinal) ? 1 : 0)
                .ToList();

            JsonSerializer.Serialize(writer, ordered, componentSerializerOptions);

            writer.WriteEndObject();
        }

        private abstract class Fail : JsonThrowHelper<SceneNode>
        {
            public static new void MissingRequiredProperty(string propertyName)
                => JsonThrowHelper<SceneNode>.MissingRequiredProperty(propertyName);
        }
    }
}
