// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Globalization;
using System.Text;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Reads image pixel dimensions from the file header without decoding pixels.</summary>
public static class ImageDimensions
{
    private const int HeaderBytes = 64 * 1024;

    /// <summary>Reads the width and height of a PNG, JPEG, BMP, TGA or Radiance HDR image.</summary>
    /// <param name="path">The image file.</param>
    /// <returns>The dimensions, or null when the header cannot be read.</returns>
    public static (int Width, int Height)? TryRead(string path)
    {
        try
        {
            using var stream = File.OpenRead(path);
            var buffer = new byte[(int)Math.Min(HeaderBytes, stream.Length)];
            stream.ReadExactly(buffer);
            return Path.GetExtension(path).ToLowerInvariant() switch
            {
                ".png" => Png(buffer),
                ".jpg" or ".jpeg" => Jpeg(buffer),
                ".bmp" => Bmp(buffer),
                ".tga" => Tga(buffer),
                ".hdr" => Radiance(buffer),
                _ => null,
            };
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or EndOfStreamException)
        {
            return null;
        }
    }

    private static (int Width, int Height)? Png(byte[] data)
        => data.Length >= 24 && data[12] == (byte)'I' && data[13] == (byte)'H' && data[14] == (byte)'D' && data[15] == (byte)'R'
            ? (BinaryPrimitives.ReadInt32BigEndian(data.AsSpan(16)), BinaryPrimitives.ReadInt32BigEndian(data.AsSpan(20)))
            : null;

    private static (int Width, int Height)? Bmp(byte[] data)
        => data.Length >= 26 && data[0] == (byte)'B' && data[1] == (byte)'M'
            ? (BinaryPrimitives.ReadInt32LittleEndian(data.AsSpan(18)), Math.Abs(BinaryPrimitives.ReadInt32LittleEndian(data.AsSpan(22))))
            : null;

    private static (int Width, int Height)? Tga(byte[] data)
        => data.Length >= 18
            ? (BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(12)), BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(14)))
            : null;

    // Walks JPEG segments to the first start-of-frame marker.
    private static (int Width, int Height)? Jpeg(byte[] data)
    {
        var offset = 2;
        while (offset + 9 < data.Length)
        {
            if (data[offset] != 0xFF)
            {
                return null;
            }

            var marker = data[offset + 1];
            var length = BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(offset + 2));
            var isStartOfFrame = marker is >= 0xC0 and <= 0xCF and not 0xC4 and not 0xC8 and not 0xCC;
            if (isStartOfFrame)
            {
                return (BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(offset + 7)), BinaryPrimitives.ReadUInt16BigEndian(data.AsSpan(offset + 5)));
            }

            offset += 2 + length;
        }

        return null;
    }

    // The resolution line follows the blank line ending the text header, as in "-Y 512 +X 1024".
    private static (int Width, int Height)? Radiance(byte[] data)
    {
        var text = Encoding.ASCII.GetString(data);
        var end = text.IndexOf("\n\n", StringComparison.Ordinal);
        if (end < 0)
        {
            return null;
        }

        var lineEnd = text.IndexOf('\n', end + 2);
        var parts = text[(end + 2)..(lineEnd < 0 ? text.Length : lineEnd)].Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length != 4
            || !int.TryParse(parts[1], NumberStyles.None, CultureInfo.InvariantCulture, out var first)
            || !int.TryParse(parts[3], NumberStyles.None, CultureInfo.InvariantCulture, out var second))
        {
            return null;
        }

        return parts[0].EndsWith('Y') ? (second, first) : (first, second);
    }
}
