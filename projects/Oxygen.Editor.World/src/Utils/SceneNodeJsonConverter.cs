// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Utils;

/// <summary>
///     A custom JSON converter for <see cref="SceneNode" /> because we want to enforce that a
///     <c>SceneNode</c> can only be created with the <see cref="Scene" /> to which it belongs.
/// </summary>
internal sealed class SceneNodeJsonConverter(Scene scene) : JsonConverter<SceneNode>
{
    /// <inheritdoc />
    public override SceneNode Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        var nodeElement = JsonSerializer.Deserialize<JsonElement>(ref reader, options);

        var name = GetRequiredString(nameof(SceneNode.Name));
        var id = GetIdOrNew();

        var sceneNode = new SceneNode(scene) { Name = name, Id = id };

        LoadComponents();
        EnsureTransform();
        LoadChildren();

        // Flags
        sceneNode.IsActive = GetBool(nameof(SceneNode.IsActive));
        sceneNode.IsVisible = GetBool(nameof(SceneNode.IsVisible));
        sceneNode.CastsShadows = GetBool(nameof(SceneNode.CastsShadows));
        sceneNode.ReceivesShadows = GetBool(nameof(SceneNode.ReceivesShadows));
        sceneNode.IsRayCastingSelectable = GetBool(nameof(SceneNode.IsRayCastingSelectable));
        sceneNode.IgnoreParentTransform = GetBool(nameof(SceneNode.IgnoreParentTransform));
        sceneNode.IsStatic = GetBool(nameof(SceneNode.IsStatic));

        return sceneNode;

        string GetRequiredString(string prop)
        {
            if (!nodeElement.TryGetProperty(prop, out var el))
            {
                Fail.MissingRequiredProperty(prop);
            }

            return el.ToString() ?? string.Empty;
        }

        Guid GetIdOrNew()
        {
            return nodeElement.TryGetProperty(nameof(GameObject.Id), out var el) && el.TryGetGuid(out var parsed)
                ? parsed
                : Guid.NewGuid();
        }

        bool GetBool(string prop)
            => nodeElement.TryGetProperty(prop, out var el) && el.GetBoolean();

        void LoadComponents()
        {
            if (!nodeElement.TryGetProperty(nameof(SceneNode.Components), out var elComponents)
                || elComponents.ValueKind != JsonValueKind.Array)
            {
                return;
            }

            sceneNode.Components.Clear();
            foreach (var elComponent in elComponents.EnumerateArray())
            {
                var component = GameComponent.FromJson(elComponent.GetRawText());
                if (component == null)
                {
                    continue;
                }

                component.Node = sceneNode;
                sceneNode.Components.Add(component);
            }
        }

        void EnsureTransform()
        {
            if (!sceneNode.Components.OfType<Transform>().Any())
            {
                sceneNode.Components.Add(new Transform(sceneNode) { Name = nameof(Transform) });
            }
        }

        void LoadChildren()
        {
            if (!nodeElement.TryGetProperty(nameof(SceneNode.Children), out var elChildren)
                || elChildren.ValueKind != JsonValueKind.Array)
            {
                return;
            }

            foreach (var elChild in elChildren.EnumerateArray())
            {
                var childNode = JsonSerializer.Deserialize<SceneNode>(elChild.GetRawText(), options);
                if (childNode != null)
                {
                    sceneNode.AddChild(childNode);
                }
            }
        }
    }

    /// <inheritdoc />
    public override void Write(Utf8JsonWriter writer, SceneNode value, JsonSerializerOptions options)
    {
        writer.WriteStartObject();

        writer.WriteString(nameof(SceneNode.Name), value.Name);
        writer.WriteString(nameof(GameObject.Id), value.Id);
        writer.WriteBoolean(nameof(SceneNode.IsActive), value.IsActive);
        writer.WriteBoolean(nameof(SceneNode.IsVisible), value.IsVisible);
        writer.WriteBoolean(nameof(SceneNode.CastsShadows), value.CastsShadows);
        writer.WriteBoolean(nameof(SceneNode.ReceivesShadows), value.ReceivesShadows);
        writer.WriteBoolean(nameof(SceneNode.IsRayCastingSelectable), value.IsRayCastingSelectable);
        writer.WriteBoolean(nameof(SceneNode.IgnoreParentTransform), value.IgnoreParentTransform);
        writer.WriteBoolean(nameof(SceneNode.IsStatic), value.IsStatic);

        writer.WritePropertyName(nameof(SceneNode.Children));
        JsonSerializer.Serialize(writer, value.Children, options);

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
