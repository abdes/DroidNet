// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Reads and writes a <see cref="NodeReference"/> as its canonical form.
/// </summary>
public sealed class NodeReferenceJsonConverter : JsonConverter<NodeReference>
{
    /// <inheritdoc/>
    public override NodeReference Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        var text = reader.TokenType == JsonTokenType.String ? reader.GetString() : null;
        return NodeReference.TryParse(text, out var reference)
            ? reference
            : throw new JsonException($"'{text}' is not a node reference: a node ID, then each node path element, joined by '/'.");
    }

    /// <inheritdoc/>
    public override void Write(Utf8JsonWriter writer, NodeReference value, JsonSerializerOptions options)
    {
        ArgumentNullException.ThrowIfNull(writer);
        ArgumentNullException.ThrowIfNull(value);
        writer.WriteStringValue(value.ToString());
    }
}
