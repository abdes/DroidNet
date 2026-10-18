// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;

namespace Oxygen.Managed.Core;

/// <summary>Stores a <see cref="FileSystemPath"/> as its full path, and refuses a stored path that is not fully qualified.</summary>
public sealed class FileSystemPathJsonConverter : JsonConverter<FileSystemPath>
{
    /// <inheritdoc/>
    public override FileSystemPath Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        var text = reader.GetString();
        return !string.IsNullOrWhiteSpace(text) && Path.IsPathFullyQualified(text)
            ? FileSystemPath.From(text)
            : throw new JsonException($"'{text}' is not a fully qualified path.");
    }

    /// <inheritdoc/>
    public override void Write(Utf8JsonWriter writer, FileSystemPath value, JsonSerializerOptions options)
    {
        ArgumentNullException.ThrowIfNull(writer);
        ArgumentNullException.ThrowIfNull(value);
        writer.WriteStringValue(value.FullPath);
    }
}
