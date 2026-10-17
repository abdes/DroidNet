// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.Json.Serialization.Metadata;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>The JSON form of scene edits: what a remote client sends and what the history could store.</summary>
/// <remarks>
/// <para>
/// <see cref="Options"/> round-trips every public <see cref="ISceneEdit"/> and every revert an edit returns. An edit is
/// written as an object whose <c>$edit</c> member names its type; read it as <see cref="ISceneEdit"/>.
/// </para>
/// <para>
/// Property values are written as their descriptor's value type and read back the same way; vectors and quaternions use the
/// scene file's form, and script slots their saved form.
/// </para>
/// </remarks>
public static class SceneEditJson
{
    /// <summary>The name of the member that names an edit's type.</summary>
    public const string TypeMember = "$edit";

    /// <summary>Gets the options that read and write scene edits.</summary>
    public static JsonSerializerOptions Options { get; } = CreateOptions();

    private static JsonSerializerOptions CreateOptions()
    {
        var options = new JsonSerializerOptions
        {
            TypeInfoResolver = new DefaultJsonTypeInfoResolver { Modifiers = { DescribeEdits } },
            Converters =
            {
                new Vector3JsonConverter(),
                new Vector4JsonConverter(),
                new QuaternionJsonConverter(),
                new PropertyEditJsonConverter(),
                new ScriptSlotJsonConverter(),
            },
        };
        options.MakeReadOnly();
        return options;
    }

    // Every public edit type of this module is a derived type of ISceneEdit.
    private static void DescribeEdits(JsonTypeInfo info)
    {
        if (info.Type != typeof(ISceneEdit))
        {
            return;
        }

        info.PolymorphismOptions = new JsonPolymorphismOptions
        {
            TypeDiscriminatorPropertyName = TypeMember,
            UnknownDerivedTypeHandling = JsonUnknownDerivedTypeHandling.FailSerialization,
        };
        foreach (var type in typeof(ISceneEdit).Assembly.GetExportedTypes()
            .Where(static type => type.IsClass && !type.IsAbstract && typeof(ISceneEdit).IsAssignableFrom(type))
            .OrderBy(static type => type.Name, StringComparer.Ordinal))
        {
            info.PolymorphismOptions.DerivedTypes.Add(new JsonDerivedType(type, type.Name));
        }
    }

    /// <summary>Writes a property edit as its entries, each value in its descriptor's value type.</summary>
    private sealed class PropertyEditJsonConverter : JsonConverter<PropertyEdit>
    {
        public override PropertyEdit Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            if (reader.TokenType != JsonTokenType.StartArray)
            {
                throw new JsonException("A property edit is an array of entries.");
            }

            var values = new PropertyEdit.Builder();
            while (reader.Read() && reader.TokenType != JsonTokenType.EndArray)
            {
                using var entry = JsonDocument.ParseValue(ref reader);
                var root = entry.RootElement;
                var id = new PropertyId(root.GetProperty("component").GetString()!, root.GetProperty("pointer").GetString()!);
                var descriptor = SceneProperties.RulesOf(id.ComponentKind)?.Descriptors.GetValueOrDefault(id)
                    ?? throw new JsonException($"Unknown scene property {id}.");
                _ = values.SetRaw(id, root.GetProperty("value").Deserialize(descriptor.ValueType, options));
            }

            return values.Build();
        }

        public override void Write(Utf8JsonWriter writer, PropertyEdit value, JsonSerializerOptions options)
        {
            writer.WriteStartArray();
            foreach (var (id, entry) in value.OrderBy(static pair => pair.Key.ToString(), StringComparer.Ordinal))
            {
                var descriptor = SceneProperties.RulesOf(id.ComponentKind)?.Descriptors.GetValueOrDefault(id)
                    ?? throw new JsonException($"Unknown scene property {id}.");
                writer.WriteStartObject();
                writer.WriteString("component", id.ComponentKind);
                writer.WriteString("pointer", id.JsonPointer);
                writer.WritePropertyName("value");
                JsonSerializer.Serialize(writer, entry, descriptor.ValueType, options);
                writer.WriteEndObject();
            }

            writer.WriteEndArray();
        }
    }

    /// <summary>Writes a script slot in its saved form, overrides included.</summary>
    private sealed class ScriptSlotJsonConverter : JsonConverter<ScriptSlot>
    {
        public override ScriptSlot Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            var data = JsonSerializer.Deserialize(ref reader, SceneJsonContext.Default.ScriptSlotData)
                ?? throw new JsonException("A script slot cannot be null.");
            var scripting = (ScriptingComponent)GameComponent.CreateAndHydrate(new ScriptingData { Name = ScriptingComponent.DefaultName, Slots = [data] });
            return scripting.Slots[0];
        }

        public override void Write(Utf8JsonWriter writer, ScriptSlot value, JsonSerializerOptions options)
        {
            var scripting = new ScriptingComponent { Name = ScriptingComponent.DefaultName, Slots = [value] };
            JsonSerializer.Serialize(writer, ((ScriptingData)scripting.Dehydrate()).Slots[0], SceneJsonContext.Default.ScriptSlotData);
        }
    }
}
