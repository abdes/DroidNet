// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Import.Textures;
using Oxygen.Assets.Persistence.LooseCooked.V1;
using SixLabors.ImageSharp;
using SixLabors.ImageSharp.PixelFormats;
using SixLabors.ImageSharp.Processing;

namespace Oxygen.Assets.Cook;

internal static class TextureCooker
{
    // Matches runtime contract:
    // - TextureResourceDesc.alignment is 256
    // - Upload row pitch alignment is 256
    // - Upload placement alignment is 512
    private const ushort TextureAlignmentBytes = 256;
    private const int RowPitchAlignmentBytes = 256;
    private const int PlacementAlignmentBytes = 512;

    public static TextureCookInput Cook(AssetKey assetKey, TextureSourceData source, ReadOnlySpan<byte> snapshotBytes)
    {
        ArgumentNullException.ThrowIfNull(source);

        var (textureType, depth, arrayLayers, compressionType, format, generateMips) = ParseSettings(source);

        using var baseImage = Image.Load<Rgba32>(snapshotBytes);
        var (width, height) = GetDimensions(baseImage);
        var data = BuildTextureData(baseImage, generateMips, out var mipLevels);

        return new TextureCookInput(
            AssetKey: assetKey,
            DataBytes: data,
            Width: width,
            Height: height,
            Depth: depth,
            ArrayLayers: arrayLayers,
            MipLevels: mipLevels,
            TextureType: textureType,
            CompressionType: compressionType,
            Format: format,
            Alignment: TextureAlignmentBytes);
    }

    private static (uint width, uint height) GetDimensions(Image<Rgba32> image)
    {
        var width = (uint)image.Width;
        var height = (uint)image.Height;
        if (width == 0 || height == 0)
        {
            throw new InvalidDataException("Texture image has invalid dimensions.");
        }

        return (width, height);
    }

    private static byte[] BuildTextureData(Image<Rgba32> baseImage, bool generateMips, out ushort mipLevels)
    {
        using var dataStream = new MemoryStream();
        WriteMipChain2D(dataStream, baseImage, generateMips, out mipLevels);
        return dataStream.ToArray();
    }

    private static void WriteMipChain2D(Stream stream, Image<Rgba32> baseImage, bool generateMips, out ushort mipLevels)
    {
        mipLevels = 0;
        var current = baseImage;
        try
        {
            while (true)
            {
                mipLevels++;
                if (mipLevels > 16384)
                {
                    throw new InvalidDataException("Mip generation produced an unreasonable number of levels.");
                }

                WriteMip2D(stream, current);

                if (!generateMips || (current.Width == 1 && current.Height == 1))
                {
                    break;
                }

                AlignStream(stream, PlacementAlignmentBytes);

                var nextWidth = Math.Max(1, current.Width / 2);
                var nextHeight = Math.Max(1, current.Height / 2);
                var next = current.Clone(ctx => ctx.Resize(nextWidth, nextHeight));

                if (!ReferenceEquals(current, baseImage))
                {
                    current.Dispose();
                }

                current = next;
            }
        }
        finally
        {
            if (!ReferenceEquals(current, baseImage))
            {
                current.Dispose();
            }
        }
    }

    private static (byte textureType, ushort depth, ushort arrayLayers, byte compressionType, byte format, bool generateMips)
        ParseSettings(TextureSourceData source)
    {
        var (textureType, depth, arrayLayers) = ParseTextureType(source.TextureType);
        var compressionType = ParseCompressionType(source.RuntimeFormat.Compression);
        var format = ParseFormat(source.RuntimeFormat.Format, source.ColorSpace);
        var generateMips = ParseMipPolicy(source.MipPolicy.Mode);
        return (textureType, depth, arrayLayers, compressionType, format, generateMips);
    }

    private static bool ParseMipPolicy(string mode)
        => NormalizeToken(mode) switch
        {
            "NONE" => false,
            "GENERATE" => true,
            _ => throw new InvalidDataException($"Unsupported MipPolicy.Mode '{mode}'."),
        };

    private static (byte textureType, ushort depth, ushort arrayLayers) ParseTextureType(string textureType)
        => NormalizeToken(textureType) switch
        {
            // oxygen::TextureType::kTexture2D = 3
            "TEXTURE2D" => (3, depth: 1, arrayLayers: 1),
            _ => throw new InvalidDataException($"Unsupported TextureType '{textureType}'."),
        };

    private static byte ParseCompressionType(string compression)
        => NormalizeToken(compression) switch
        {
            "NONE" => 0,
            _ => throw new NotSupportedException($"Unsupported texture compression '{compression}'."),
        };

    private static byte ParseFormat(string format, string colorSpace)
    {
        var fmt = NormalizeToken(format);
        var cs = NormalizeToken(colorSpace);

        if (cs is not ("SRGB" or "LINEAR"))
        {
            throw new InvalidDataException($"Unsupported ColorSpace '{colorSpace}'.");
        }

        // oxygen::Format:
        // - kRGBA8UNorm = 30
        // - kRGBA8UNormSRGB = 31
        var isSrgb = string.Equals(cs, "SRGB", StringComparison.Ordinal);

        return fmt switch
        {
            "R8G8B8A8UNORM" or "RGBA8UNORM" => (byte)(isSrgb ? 31 : 30),
            "R8G8B8A8UNORMSRGB" or "RGBA8UNORMSRGB" => isSrgb
                ? (byte)31
                : throw new InvalidDataException("Format is sRGB but ColorSpace is Linear."),
            _ => throw new NotSupportedException($"Unsupported runtime texture format '{format}'."),
        };
    }

    private static void WriteMip2D(Stream stream, Image<Rgba32> image)
    {
        var rowBytes = image.Width * 4;
        var rowPitch = Align(rowBytes, RowPitchAlignmentBytes);
        var padding = rowPitch - rowBytes;

        var pixels = new byte[rowBytes * image.Height];
        image.CopyPixelDataTo(pixels);

        for (var y = 0; y < image.Height; y++)
        {
            var rowStart = y * rowBytes;
            stream.Write(pixels, rowStart, rowBytes);
            if (padding != 0)
            {
                stream.Write(new byte[padding]);
            }
        }
    }

    private static int Align(int value, int alignment)
        => alignment <= 1 ? value : (value + alignment - 1) / alignment * alignment;

    private static void AlignStream(Stream stream, int alignmentBytes)
    {
        if (alignmentBytes <= 1)
        {
            return;
        }

        var padding = (alignmentBytes - (stream.Position % alignmentBytes)) % alignmentBytes;
        if (padding == 0)
        {
            return;
        }

        stream.Write(new byte[padding]);
    }

    private static string NormalizeToken(string value)
        => string.IsNullOrWhiteSpace(value)
            ? string.Empty
            : value
                .Replace("_", string.Empty, StringComparison.Ordinal)
                .Replace("-", string.Empty, StringComparison.Ordinal)
                .Replace(" ", string.Empty, StringComparison.Ordinal)
                .Trim()
                .ToUpperInvariant();
}
