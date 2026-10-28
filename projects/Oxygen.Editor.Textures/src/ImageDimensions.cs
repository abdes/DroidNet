// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Globalization;
using System.IO.Abstractions;
using System.Text;

namespace Oxygen.Editor.Textures;

/// <summary>Reads image pixel dimensions from the file header without decoding pixels.</summary>
internal static class ImageDimensions
{
    private const int HeaderBytes = 64 * 1024;

    private static ReadOnlySpan<byte> OpenExrMagic => [0x76, 0x2F, 0x31, 0x01];

    private static ReadOnlySpan<byte> Ktx2Identifier => [0xAB, (byte)'K', (byte)'T', (byte)'X', (byte)' ', (byte)'2', (byte)'0', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A];

    /// <summary>Reads the width and height of a PNG, JPEG, BMP, TGA, Radiance HDR, OpenEXR, WebP or KTX2 image.</summary>
    /// <param name="fileSystem">The file system that holds the image.</param>
    /// <param name="path">The image file.</param>
    /// <returns>The dimensions, or null when the header cannot be read.</returns>
    public static (int Width, int Height)? TryRead(IFileSystem fileSystem, string path)
    {
        try
        {
            using var stream = fileSystem.File.OpenRead(path);
            var buffer = new byte[(int)Math.Min(HeaderBytes, stream.Length)];
            stream.ReadExactly(buffer);
            var extension = Path.GetExtension(path);
            return Is(".png") ? Png(buffer)
                : Is(".jpg") || Is(".jpeg") ? Jpeg(buffer)
                : Is(".bmp") ? Bmp(buffer)
                : Is(".tga") ? Tga(buffer)
                : Is(".hdr") ? Radiance(buffer)
                : Is(".exr") ? OpenExr(buffer)
                : Is(".webp") ? Webp(buffer)
                : Is(".ktx2") ? Ktx2(buffer)
                : null;

            bool Is(string suffix) => string.Equals(extension, suffix, StringComparison.OrdinalIgnoreCase);
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

    // The first chunk of the RIFF "WEBP" form holds the size: a lossy "VP8 " frame header, a lossless "VP8L" header or
    // an extended "VP8X" canvas.
    private static (int Width, int Height)? Webp(byte[] data)
    {
        if (data.Length < 30 || !data.AsSpan(0, 4).SequenceEqual("RIFF"u8) || !data.AsSpan(8, 4).SequenceEqual("WEBP"u8))
        {
            return null;
        }

        var chunk = data.AsSpan(12, 4);
        if (chunk.SequenceEqual("VP8 "u8))
        {
            return (BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(26)) & 0x3FFF, BinaryPrimitives.ReadUInt16LittleEndian(data.AsSpan(28)) & 0x3FFF);
        }

        if (chunk.SequenceEqual("VP8L"u8))
        {
            var bits = BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(21));
            return ((int)(bits & 0x3FFF) + 1, (int)((bits >> 14) & 0x3FFF) + 1);
        }

        return chunk.SequenceEqual("VP8X"u8) ? (ReadUInt24(data, 24) + 1, ReadUInt24(data, 27) + 1) : null;

        static int ReadUInt24(byte[] bytes, int offset) => bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16);
    }

    // After the magic number and version, the OpenEXR header is a list of attributes (name, type, byte size, value)
    // ended by an empty name; the "dataWindow" box2i holds xMin, yMin, xMax and yMax, inclusive.
    private static (int Width, int Height)? OpenExr(byte[] data)
    {
        if (data.Length < 8 || !data.AsSpan(0, 4).SequenceEqual(OpenExrMagic))
        {
            return null;
        }

        var offset = 8;
        while (offset < data.Length && data[offset] != 0)
        {
            var name = ReadString(data, ref offset);
            var type = ReadString(data, ref offset);
            if (name is null || type is null || offset + 4 > data.Length)
            {
                return null;
            }

            var size = BinaryPrimitives.ReadInt32LittleEndian(data.AsSpan(offset));
            offset += 4;
            if (size < 0 || offset + size > data.Length)
            {
                return null;
            }

            if (name == "dataWindow" && type == "box2i" && size == 16)
            {
                var box = data.AsSpan(offset);
                return (BinaryPrimitives.ReadInt32LittleEndian(box[8..]) - BinaryPrimitives.ReadInt32LittleEndian(box) + 1,
                    BinaryPrimitives.ReadInt32LittleEndian(box[12..]) - BinaryPrimitives.ReadInt32LittleEndian(box[4..]) + 1);
            }

            offset += size;
        }

        return null;

        static string? ReadString(byte[] bytes, ref int position)
        {
            var end = Array.IndexOf(bytes, (byte)0, position);
            if (end < 0)
            {
                return null;
            }

            var text = Encoding.ASCII.GetString(bytes, position, end - position);
            position = end + 1;
            return text;
        }
    }

    // The KTX 2.0 header follows its 12-byte identifier with vkFormat, typeSize, pixelWidth and pixelHeight.
    private static (int Width, int Height)? Ktx2(byte[] data)
        => data.Length >= 28 && data.AsSpan(0, 12).SequenceEqual(Ktx2Identifier)
            ? ((int)BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(20)), (int)BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(24)))
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
