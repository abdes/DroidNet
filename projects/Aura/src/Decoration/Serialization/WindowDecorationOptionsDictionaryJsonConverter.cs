// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;

namespace DroidNet.Aura.Decoration.Serialization;

/// <summary>
///     Custom converter for serializing/deserializing a dictionary keyed by <see cref="WindowCategory"/>
///     where the value is a <see cref="WindowDecorationOptions"/>. The converter ensures the category
///     appears only as the dictionary key in JSON and is injected into the value during deserialization.
/// </summary>
public sealed class WindowDecorationOptionsDictionaryJsonConverter : JsonConverter<Dictionary<WindowCategory, WindowDecorationOptions>>
{
    /// <inheritdoc/>
    public override Dictionary<WindowCategory, WindowDecorationOptions>? Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        if (reader.TokenType == JsonTokenType.Null)
        {
            return null;
        }

        if (reader.TokenType != JsonTokenType.StartObject)
        {
            throw new JsonException("Expected start of object for WindowDecorationOptions dictionary.");
        }

        var result = new Dictionary<WindowCategory, WindowDecorationOptions>();

        while (reader.Read())
        {
            if (reader.TokenType == JsonTokenType.EndObject)
            {
                break;
            }

            var category = ReadCategoryKey(ref reader);
            var value = ReadCategoryValue(ref reader, options, category);
            result[category] = value;
        }

        return result;
    }

    /// <inheritdoc/>
    public override void Write(Utf8JsonWriter writer, Dictionary<WindowCategory, WindowDecorationOptions> value, JsonSerializerOptions options)
    {
        if (value is null)
        {
            writer.WriteNullValue();
            return;
        }

        writer.WriteStartObject();

        foreach (var kvp in value)
        {
            // Use the canonical category string as the property name
            writer.WritePropertyName(kvp.Key.Value);

            // Serialize the value to a JsonElement first, then write all properties
            // except the `category` property so we don't duplicate the category
            // inside the object when it's already the dictionary key.
            var element = JsonSerializer.SerializeToElement(kvp.Value, options);

            if (element.ValueKind != JsonValueKind.Object)
            {
                // Fallback: write the element directly if it's not an object
                element.WriteTo(writer);
            }
            else
            {
                writer.WriteStartObject();

                foreach (var prop in element.EnumerateObject())
                {
                    // Skip the category property (JSON property names are camel-cased by context)
                    if (string.Equals(prop.Name, "category", StringComparison.OrdinalIgnoreCase))
                    {
                        continue;
                    }

                    prop.WriteTo(writer);
                }

                writer.WriteEndObject();
            }
        }

        writer.WriteEndObject();
    }

    private static WindowCategory ReadCategoryKey(ref Utf8JsonReader reader)
    {
        if (reader.TokenType != JsonTokenType.PropertyName)
        {
            throw new JsonException("Expected property name while reading WindowDecorationOptions dictionary.");
        }

        var propertyName = reader.GetString();
        if (string.IsNullOrWhiteSpace(propertyName))
        {
            throw new JsonException("WindowCategory key cannot be null or whitespace.");
        }

        return new WindowCategory(propertyName);
    }

    private static WindowDecorationOptions ReadCategoryValue(ref Utf8JsonReader reader, JsonSerializerOptions options, WindowCategory category)
    {
        if (!reader.Read())
        {
            throw new JsonException($"Unexpected end of JSON payload while reading category '{category.Value}'.");
        }

        using var doc = JsonDocument.ParseValue(ref reader);
        if (doc.RootElement.ValueKind != JsonValueKind.Object)
        {
            throw new JsonException($"Expected object for WindowDecorationOptions value for category '{category.Value}'.");
        }

        var node = JsonNode.Parse(doc.RootElement.GetRawText()) as JsonObject
            ?? throw new JsonException($"Failed to parse WindowDecorationOptions JSON node for category '{category.Value}'.");

        EnsureCategoryProperty(node, category);

        return node.Deserialize<WindowDecorationOptions>(options)
            ?? throw new JsonException($"Failed to deserialize WindowDecorationOptions for category '{category.Value}'.");
    }

    private static void EnsureCategoryProperty(JsonObject node, WindowCategory category)
    {
        if (node.TryGetPropertyValue("category", out var existingCategoryNode) && existingCategoryNode is JsonValue existingCategoryValue)
        {
            var existingCategory = existingCategoryValue.GetValue<string>();
            if (!string.Equals(existingCategory, category.Value, StringComparison.Ordinal))
            {
                throw new JsonException($"Category key '{category.Value}' does not match embedded category '{existingCategory}'.");
            }

            return;
        }

        node["category"] = category.Value;
    }
}
