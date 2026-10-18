// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;

namespace Oxygen.Managed.Core;

/// <summary>Stores an <see cref="AssetPath"/> as its asset URI, as a value and as a property name.</summary>
public sealed class AssetPathJsonConverter : JsonConverter<AssetPath>
{
    /// <inheritdoc/>
    public override AssetPath Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        => Parse(reader.GetString());

    /// <inheritdoc/>
    public override void Write(Utf8JsonWriter writer, AssetPath value, JsonSerializerOptions options)
    {
        ArgumentNullException.ThrowIfNull(writer);
        ArgumentNullException.ThrowIfNull(value);
        writer.WriteStringValue(value.ToUriString());
    }

    /// <inheritdoc/>
    public override AssetPath ReadAsPropertyName(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        => Parse(reader.GetString());

    /// <inheritdoc/>
    public override void WriteAsPropertyName(Utf8JsonWriter writer, AssetPath value, JsonSerializerOptions options)
    {
        ArgumentNullException.ThrowIfNull(writer);
        ArgumentNullException.ThrowIfNull(value);
        writer.WritePropertyName(value.ToUriString());
    }

    private static AssetPath Parse(string? text)
        => AssetPath.TryParseUri(text, out var path)
            ? path
            : throw new JsonException($"'{text}' is not an '{AssetPath.UriScheme}' URI naming a path the rules accept.");
}
