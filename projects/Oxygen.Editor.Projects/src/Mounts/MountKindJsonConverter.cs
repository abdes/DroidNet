// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;

namespace Oxygen.Editor.Projects.Mounts;

/// <summary>Reads a mount kind by name, and refuses any other with <see cref="MountCodes.KindUnknown"/>.</summary>
internal sealed class MountKindJsonConverter : JsonConverter<MountKind>
{
    /// <inheritdoc/>
    public override MountKind Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        var text = reader.TokenType == JsonTokenType.String ? reader.GetString() : null;
        return text switch
        {
            nameof(MountKind.Source) => MountKind.Source,
            nameof(MountKind.Cooked) => MountKind.Cooked,
            _ => throw new JsonException($"{MountCodes.KindUnknown}: a mount has the kind '{text}'; a mount is Source or Cooked."),
        };
    }

    /// <inheritdoc/>
    public override void Write(Utf8JsonWriter writer, MountKind value, JsonSerializerOptions options)
    {
        ArgumentNullException.ThrowIfNull(writer);
        writer.WriteStringValue(value.ToString());
    }
}
