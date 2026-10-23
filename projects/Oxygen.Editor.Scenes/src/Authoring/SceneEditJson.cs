// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.Json.Serialization.Metadata;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;
using Oxygen.Managed.Core;

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
                new OverrideWriteJsonConverter(),
                new ScriptSlotJsonConverter(),
                new ScriptValueJsonConverter(),
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
    // A value is written as its property's type, as a scene file writes it.
    private sealed class OverrideWriteJsonConverter : JsonConverter<OverrideWrite>
    {
        public override OverrideWrite Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            using var entry = JsonDocument.ParseValue(ref reader);
            var root = entry.RootElement;
            var partKind = root.GetProperty("part").GetString();
            var property = new PropertyId(root.GetProperty("component").GetString()!, partKind, root.GetProperty("pointer").GetString()!);
            var value = root.GetProperty("value");
            var owner = root.TryGetProperty("owner", out var ownerJson) ? ownerJson.Deserialize(SceneJsonContext.Default.AssetPath) : null;
            return new OverrideWrite(
                property,
                partKind is null ? null : new PartId(partKind, root.GetProperty("partId").GetGuid()),
                root.GetProperty("revision").GetString(),
                value.ValueKind == JsonValueKind.Null ? null : SourceOverridesJson.ReadValue(property, value))
            {
                PartOwner = owner,
            };
        }

        public override void Write(Utf8JsonWriter writer, OverrideWrite value, JsonSerializerOptions options)
        {
            writer.WriteStartObject();
            writer.WriteString("component", value.Property.ComponentKind);
            writer.WriteString("part", value.Property.PartKind);
            writer.WriteString("pointer", value.Property.JsonPointer);
            if (value.Part is { } part)
            {
                writer.WriteString("partId", part.Id);
            }

            writer.WriteString("revision", value.LayoutRevision);
            if (value.PartOwner is { } owner)
            {
                writer.WritePropertyName("owner");
                JsonSerializer.Serialize(writer, owner, SceneJsonContext.Default.AssetPath);
            }

            writer.WritePropertyName("value");
            if (value.Value is null)
            {
                writer.WriteNullValue();
            }
            else
            {
                SourceOverridesJson.WriteValue(value.Property, value.Value).WriteTo(writer);
            }

            writer.WriteEndObject();
        }
    }

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

    /// <summary>Writes a script parameter value in its saved form.</summary>
    private sealed class ScriptValueJsonConverter : JsonConverter<ScriptValue>
    {
        // A value is saved as the override of a slot, whose saved form is the value's.
        private const string Name = "value";

        public override ScriptValue Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            var data = JsonSerializer.Deserialize(ref reader, SceneJsonContext.Default.ScriptValueData)
                ?? throw new JsonException("A script value cannot be null.");
            var slot = new ScriptSlotData { Id = Guid.NewGuid(), Overrides = new Dictionary<string, ScriptValueData>(StringComparer.Ordinal) { [Name] = data } };
            var scripting = (ScriptingComponent)GameComponent.CreateAndHydrate(new ScriptingData { Name = ScriptingComponent.DefaultName, Slots = [slot] });
            return scripting.Slots[0].Overrides[Name];
        }

        public override void Write(Utf8JsonWriter writer, ScriptValue value, JsonSerializerOptions options)
        {
            var scripting = new ScriptingComponent { Name = ScriptingComponent.DefaultName, Slots = [new ScriptSlot().WithOverride(Name, value)] };
            JsonSerializer.Serialize(writer, ((ScriptingData)scripting.Dehydrate()).Slots[0].Overrides![Name], SceneJsonContext.Default.ScriptValueData);
        }
    }
}
