// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;

namespace Oxygen.Editor.Models;

/// <summary>What the last import of a model read: its primary source file and every other file it read whole.</summary>
/// <param name="Size">The primary source's size, in bytes.</param>
/// <param name="Sha256">The primary source's SHA-256, lowercase hexadecimal.</param>
/// <param name="Files">The other files, relative to the primary's folder, sorted by path.</param>
public sealed record ModelSourceFingerprint(long Size, string Sha256, ImmutableArray<ModelSourceFile> Files)
{
    /// <summary>Reads a fingerprint from its JSON form.</summary>
    /// <param name="value">The <c>source_fingerprint</c> object.</param>
    /// <returns>The fingerprint.</returns>
    internal static ModelSourceFingerprint Read(JsonElement value)
        => new(
            value.GetProperty("size").GetInt64(),
            value.GetProperty("sha256").GetString()!,
            [.. value.GetProperty("files").EnumerateArray().Select(static file => new ModelSourceFile(
                file.GetProperty("path").GetString()!, file.GetProperty("size").GetInt64(), file.GetProperty("sha256").GetString()!))]);

    /// <summary>Writes the fingerprint as the record's <c>source_fingerprint</c>.</summary>
    /// <param name="writer">The record's writer.</param>
    internal void Write(Utf8JsonWriter writer)
    {
        writer.WriteStartObject("source_fingerprint");
        writer.WriteNumber("size", this.Size);
        writer.WriteString("sha256", this.Sha256);
        writer.WriteStartArray("files");
        foreach (var file in this.Files)
        {
            writer.WriteStartObject();
            writer.WriteString("path", file.Path);
            writer.WriteNumber("size", file.Size);
            writer.WriteString("sha256", file.Sha256);
            writer.WriteEndObject();
        }

        writer.WriteEndArray();
        writer.WriteEndObject();
    }
}
