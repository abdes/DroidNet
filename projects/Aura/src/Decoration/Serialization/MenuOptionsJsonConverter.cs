// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;

namespace DroidNet.Aura.Decoration.Serialization;

/// <summary>
///     Custom JSON converter for <see cref="MenuOptions"/> that serializes only the provider ID
///     and compact mode setting.
/// </summary>
/// <remarks>
///     This converter ensures that only the menu provider identifier and compact mode flag are
///     persisted to JSON. The actual MenuSource is not serializable and is resolved at runtime from
///     the DI container using the provider ID.
///     <para>
///     The converter serializes to a JSON object with the following structure:
///     <code><![CDATA[
///     {
///       "menuProviderId": "App.MainMenu",
///       "isCompact": false
///     }
///     ]]></code>
///     </para>
/// </remarks>
public sealed class MenuOptionsJsonConverter : JsonConverter<MenuOptions>
{
    /// <summary>
    ///     Reads and converts JSON to a <see cref="MenuOptions"/> instance.
    /// </summary>
    /// <param name="reader">The reader to read JSON from.</param>
    /// <param name="typeToConvert">The type to convert.</param>
    /// <param name="options">Serializer options.</param>
    /// <returns>A <see cref="MenuOptions"/> instance deserialized from JSON.</returns>
    /// <exception cref="JsonException">
    ///     Thrown if the JSON is invalid or missing required properties.
    /// </exception>
    public override MenuOptions? Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        if (reader.TokenType == JsonTokenType.Null)
        {
            return null;
        }

        if (reader.TokenType != JsonTokenType.StartObject)
        {
            throw new JsonException("Expected start of object for MenuOptions.");
        }

        string? menuProviderId = null;
        var isCompact = false;

        while (reader.Read())
        {
            if (reader.TokenType == JsonTokenType.EndObject)
            {
                break;
            }

            if (reader.TokenType == JsonTokenType.PropertyName)
            {
                var propertyName = reader.GetString();
                _ = reader.Read();

                switch (propertyName)
                {
                    case "MenuProviderId":
                    case "menuProviderId":
                        menuProviderId = reader.GetString();
                        break;
                    case "IsCompact":
                    case "isCompact":
                        isCompact = reader.GetBoolean();
                        break;
                }
            }
        }

        return string.IsNullOrWhiteSpace(menuProviderId)
            ? throw new JsonException("MenuOptions requires a non-empty menuProviderId.")
            : new MenuOptions
            {
                MenuProviderId = menuProviderId,
                IsCompact = isCompact,
            };
    }

    /// <summary>
    ///     Writes a <see cref="MenuOptions"/> instance to JSON.
    /// </summary>
    /// <param name="writer">The writer to write JSON to.</param>
    /// <param name="value">The value to serialize.</param>
    /// <param name="options">Serializer options.</param>
    /// <remarks>
    ///     Only the menu provider ID and compact mode flag are serialized. The menu source is not
    ///     persisted as it must be resolved from the DI container at runtime.
    /// </remarks>
    public override void Write(Utf8JsonWriter writer, MenuOptions value, JsonSerializerOptions options)
    {
        ArgumentNullException.ThrowIfNull(writer);
        ArgumentNullException.ThrowIfNull(value);

        writer.WriteStartObject();
        writer.WriteString("menuProviderId", value.MenuProviderId);
        writer.WriteBoolean("isCompact", value.IsCompact);
        writer.WriteEndObject();
    }
}
