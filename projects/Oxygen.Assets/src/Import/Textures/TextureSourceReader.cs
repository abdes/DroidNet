// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;

namespace Oxygen.Assets.Import.Textures;

public static class TextureSourceReader
{
    private const string ExpectedSchema = "oxygen.texture.v1";

    public static TextureSourceData Read(ReadOnlySpan<byte> jsonUtf8)
    {
        var input = JsonSerializer.Deserialize(jsonUtf8, Serialization.Context.TextureSourceData)
            ?? throw new InvalidDataException("Texture JSON is empty or invalid.");

        if (!string.Equals(input.Schema, ExpectedSchema, StringComparison.Ordinal))
        {
            throw new InvalidDataException($"Unsupported texture schema '{input.Schema}'.");
        }

        if (string.IsNullOrWhiteSpace(input.SourceImage))
        {
            throw new InvalidDataException("Texture JSON is missing required field 'SourceImage'.");
        }

        if (string.IsNullOrWhiteSpace(input.ColorSpace))
        {
            throw new InvalidDataException("Texture JSON is missing required field 'ColorSpace'.");
        }

        if (string.IsNullOrWhiteSpace(input.TextureType))
        {
            throw new InvalidDataException("Texture JSON is missing required field 'TextureType'.");
        }

        if (input.MipPolicy is null || string.IsNullOrWhiteSpace(input.MipPolicy.Mode))
        {
            throw new InvalidDataException("Texture JSON is missing required field 'MipPolicy.Mode'.");
        }

        if (input.RuntimeFormat is null || string.IsNullOrWhiteSpace(input.RuntimeFormat.Format) || string.IsNullOrWhiteSpace(input.RuntimeFormat.Compression))
        {
            throw new InvalidDataException("Texture JSON is missing required field 'RuntimeFormat'.");
        }

        return input;
    }
}
