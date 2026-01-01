// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Text;
using Oxygen.Assets.Import.Materials;

namespace Oxygen.Assets.Cook;

/// <summary>
/// Writes cooked <c>.omat</c> binary descriptors compatible with the runtime <c>MaterialAssetDesc</c>.
/// </summary>
public static class CookedMaterialWriter
{
    private const int DescriptorSize = 256;

    private const byte AssetTypeMaterial = 1;

    private const byte MaterialDomainOpaque = 1;
    private const byte MaterialDomainAlphaBlended = 2;
    private const byte MaterialDomainMasked = 3;

    private const int HeaderAssetTypeOffset = 0x00;
    private const int HeaderNameOffset = 0x01;
    private const int HeaderNameSize = 64;
    private const int HeaderVersionOffset = 0x41;
    private const int HeaderStreamingPriorityOffset = 0x42;
    private const int HeaderContentHashOffset = 0x43;
    private const int HeaderVariantFlagsOffset = 0x4B;

    private const int MaterialDomainOffset = 0x5F;
    private const int FlagsOffset = 0x60;
    private const int ShaderStagesOffset = 0x64;

    private const int BaseColorOffset = 0x68;
    private const int NormalScaleOffset = 0x78;
    private const int MetalnessOffset = 0x7C;
    private const int RoughnessOffset = 0x7E;
    private const int AmbientOcclusionOffset = 0x80;

    private const int BaseColorTextureOffset = 0x82;
    private const int NormalTextureOffset = 0x86;
    private const int MetallicTextureOffset = 0x8A;
    private const int RoughnessTextureOffset = 0x8E;
    private const int AmbientOcclusionTextureOffset = 0x92;

    private const int AlphaCutoffOffset = 0xB8;

    /// <summary>
    /// Writes a cooked <c>.omat</c> descriptor for the given material source.
    /// </summary>
    /// <param name="output">The output stream.</param>
    /// <param name="material">The authoring material source.</param>
    public static void Write(Stream output, MaterialSource material)
    {
        ArgumentNullException.ThrowIfNull(output);
        ArgumentNullException.ThrowIfNull(material);

        Span<byte> desc = stackalloc byte[DescriptorSize];
        desc.Clear();

        // AssetHeader
        desc[HeaderAssetTypeOffset] = AssetTypeMaterial;
        WriteName(desc.Slice(HeaderNameOffset, HeaderNameSize), material.Name ?? "Material");
        desc[HeaderVersionOffset] = 1;
        desc[HeaderStreamingPriorityOffset] = 0;
        BinaryPrimitives.WriteUInt64LittleEndian(desc.Slice(HeaderContentHashOffset, 8), 0);
        BinaryPrimitives.WriteUInt32LittleEndian(desc.Slice(HeaderVariantFlagsOffset, 4), 0);

        // MaterialAssetDesc
        desc[MaterialDomainOffset] = ToMaterialDomain(material.AlphaMode);
        BinaryPrimitives.WriteUInt32LittleEndian(desc.Slice(FlagsOffset, 4), ToFlags(material));
        BinaryPrimitives.WriteUInt32LittleEndian(desc.Slice(ShaderStagesOffset, 4), 0);

        var pbr = material.PbrMetallicRoughness ?? new MaterialPbrMetallicRoughness(1, 1, 1, 1, 1, 1, null, null);

        WriteSingle(desc.Slice(BaseColorOffset + 0, 4), Clamp01(pbr.BaseColorR));
        WriteSingle(desc.Slice(BaseColorOffset + 4, 4), Clamp01(pbr.BaseColorG));
        WriteSingle(desc.Slice(BaseColorOffset + 8, 4), Clamp01(pbr.BaseColorB));
        WriteSingle(desc.Slice(BaseColorOffset + 12, 4), Clamp01(pbr.BaseColorA));

        WriteSingle(desc.Slice(NormalScaleOffset, 4), Math.Max(0.0f, material.NormalTexture?.Scale ?? 1.0f));
        WriteUnorm16(desc.Slice(MetalnessOffset, 2), Clamp01(pbr.MetallicFactor));
        WriteUnorm16(desc.Slice(RoughnessOffset, 2), Clamp01(pbr.RoughnessFactor));
        WriteUnorm16(desc.Slice(AmbientOcclusionOffset, 2), Clamp01(material.OcclusionTexture?.Strength ?? 1.0f));

        WriteUnorm16(desc.Slice(AlphaCutoffOffset, 2), material.AlphaCutoff);

        // MVP: textures are not emitted yet.
        BinaryPrimitives.WriteUInt32LittleEndian(desc.Slice(BaseColorTextureOffset, 4), 0);
        BinaryPrimitives.WriteUInt32LittleEndian(desc.Slice(NormalTextureOffset, 4), 0);
        BinaryPrimitives.WriteUInt32LittleEndian(desc.Slice(MetallicTextureOffset, 4), 0);
        BinaryPrimitives.WriteUInt32LittleEndian(desc.Slice(RoughnessTextureOffset, 4), 0);
        BinaryPrimitives.WriteUInt32LittleEndian(desc.Slice(AmbientOcclusionTextureOffset, 4), 0);

        output.Write(desc);
    }

    private static byte ToMaterialDomain(MaterialAlphaMode alphaMode)
        => alphaMode switch
        {
            MaterialAlphaMode.Opaque => MaterialDomainOpaque,
            MaterialAlphaMode.Blend => MaterialDomainAlphaBlended,
            MaterialAlphaMode.Mask => MaterialDomainMasked,
            _ => MaterialDomainOpaque,
        };

    private static uint ToFlags(MaterialSource material)
    {
        // Must match Oxygen.Engine (data::pak::kMaterialFlag_*):
        // bit 1 = double-sided
        // bit 2 = alpha test
        var flags = 0u;

        if (material.DoubleSided)
        {
            flags |= 1u << 1;
        }

        if (material.AlphaMode == MaterialAlphaMode.Mask)
        {
            flags |= 1u << 2;
        }

        return flags;
    }

    private static void WriteName(Span<byte> destination, string name)
    {
        destination.Clear();

        if (string.IsNullOrEmpty(name))
        {
            return;
        }

        var bytes = Encoding.UTF8.GetBytes(name);
        var count = Math.Min(bytes.Length, destination.Length - 1);
        bytes.AsSpan(0, count).CopyTo(destination);
        destination[count] = 0;
    }

    private static void WriteSingle(Span<byte> destination, float value)
        => BinaryPrimitives.WriteUInt32LittleEndian(destination, (uint)BitConverter.SingleToInt32Bits(value));

    private static void WriteUnorm16(Span<byte> destination, float value)
    {
        var clamped = Math.Clamp(value, 0.0f, 1.0f);
        var scaled = (uint)Math.Round(clamped * 65535.0f, MidpointRounding.AwayFromZero);
        if (scaled > ushort.MaxValue)
        {
            scaled = ushort.MaxValue;
        }
        BinaryPrimitives.WriteUInt16LittleEndian(destination, (ushort)scaled);
    }

    private static float Clamp01(float value)
        => Math.Clamp(value, 0.0f, 1.0f);
}
