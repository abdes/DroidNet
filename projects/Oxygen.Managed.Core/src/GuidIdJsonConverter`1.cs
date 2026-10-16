// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;

namespace Oxygen.Managed.Core;

/// <summary>
///     Serializes a <see cref="IGuidId{TSelf}"/> as its underlying <see cref="Guid"/>, as a value and as a dictionary key,
///     so typed ids keep the JSON form of the plain identifiers they replace.
/// </summary>
/// <typeparam name="TId">The id type.</typeparam>
public sealed class GuidIdJsonConverter<TId> : JsonConverter<TId>
    where TId : struct, IGuidId<TId>
{
    /// <inheritdoc/>
    public override TId Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        => TId.FromValue(reader.GetGuid());

    /// <inheritdoc/>
    public override void Write(Utf8JsonWriter writer, TId value, JsonSerializerOptions options)
    {
        ArgumentNullException.ThrowIfNull(writer);
        writer.WriteStringValue(value.Value);
    }

    /// <inheritdoc/>
    public override TId ReadAsPropertyName(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        => TId.FromValue(Guid.Parse(reader.GetString() ?? throw new JsonException("An id property name is missing.")));

    /// <inheritdoc/>
    public override void WriteAsPropertyName(Utf8JsonWriter writer, TId value, JsonSerializerOptions options)
    {
        ArgumentNullException.ThrowIfNull(writer);
        writer.WritePropertyName(value.Value.ToString("D"));
    }
}
