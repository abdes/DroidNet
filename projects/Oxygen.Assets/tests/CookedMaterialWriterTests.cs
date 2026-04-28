// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Text;
using AwesomeAssertions;
using Oxygen.Assets.Cook;
using Oxygen.Assets.Import.Materials;

namespace Oxygen.Assets.Tests;

[TestClass]
public sealed class CookedMaterialWriterTests
{
    [TestMethod]
    public void Write_ShouldProduce256ByteDescriptorWithExpectedLayout()
    {
        var material = new MaterialSource(
            schema: "oxygen.material.v1",
            type: "PBR",
            name: "Test Material",
            pbrMetallicRoughness: new MaterialPbrMetallicRoughness(
                baseColorR: 0.1f,
                baseColorG: 0.2f,
                baseColorB: 0.3f,
                baseColorA: 0.4f,
                metallicFactor: 0.7f,
                roughnessFactor: 0.2f,
                baseColorTexture: null,
                metallicRoughnessTexture: null),
            normalTexture: new NormalTextureRef("asset:///Content/Textures/N.png", 1.5f),
            occlusionTexture: new OcclusionTextureRef("asset:///Content/Textures/AO.png", 0.9f),
            alphaMode: MaterialAlphaMode.Mask,
            alphaCutoff: 0.5f,
            doubleSided: true);

        byte[] bytes;
        using (var ms = new MemoryStream())
        {
            CookedMaterialWriter.Write(ms, material);
            bytes = ms.ToArray();
        }

        _ = bytes.Should().HaveCount(256);
        _ = bytes[0x00].Should().Be(1);
        _ = ReadNullTerminatedUtf8(bytes.AsSpan(0x01, 64)).Should().Be("Test Material");
        _ = bytes[0x41].Should().Be(1);

        // material_domain
        _ = bytes[0x5F].Should().Be(3);

        // flags: bit1 double-sided, bit2 alpha test
        var flags = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(0x60, 4));
        _ = flags.Should().Be((1u << 1) | (1u << 2));

        // scalar offsets match runtime tests (little-endian floats)
        _ = ReadSingle(bytes, 0x68).Should().BeApproximately(0.1f, 0.0001f);
        _ = ReadSingle(bytes, 0x6C).Should().BeApproximately(0.2f, 0.0001f);
        _ = ReadSingle(bytes, 0x70).Should().BeApproximately(0.3f, 0.0001f);
        _ = ReadSingle(bytes, 0x74).Should().BeApproximately(0.4f, 0.0001f);
        _ = ReadSingle(bytes, 0x78).Should().BeApproximately(1.5f, 0.0001f);
        _ = ReadUnorm16(bytes, 0x7C).Should().BeApproximately(0.7f, 0.0001f);
        _ = ReadUnorm16(bytes, 0x7E).Should().BeApproximately(0.2f, 0.0001f);
        _ = ReadUnorm16(bytes, 0x80).Should().BeApproximately(0.9f, 0.0001f);

        // MVP: texture indices are 0.
        _ = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(0x82, 4)).Should().Be(0);
        _ = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(0x86, 4)).Should().Be(0);
        _ = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(0x8A, 4)).Should().Be(0);
        _ = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(0x8E, 4)).Should().Be(0);
        _ = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(0x92, 4)).Should().Be(0);

        _ = ReadUnorm16(bytes, 0xB8).Should().BeApproximately(0.5f, 0.0001f);
    }

    [TestMethod]
    public void SourceReader_ShouldReadAndClampValues()
    {
        const string json = """
        {
            "Schema": "oxygen.material.v1",
            "Type": "PBR",
            "Name": "ClampMe",
            "PbrMetallicRoughness": {
                "BaseColorFactor": [2, -1, 0.5, 1],
                "MetallicFactor": 2,
                "RoughnessFactor": -1
            },
            "NormalTexture": { "Source": "asset:///Content/Textures/N.png", "Scale": -4 },
            "OcclusionTexture": { "Source": "asset:///Content/Textures/AO.png", "Strength": 2 },
            "AlphaMode": "OPAQUE",
            "DoubleSided": false
        }
        """;

        var material = MaterialSourceReader.Read(Encoding.UTF8.GetBytes(json));

        _ = material.PbrMetallicRoughness.BaseColorR.Should().Be(1.0f);
        _ = material.PbrMetallicRoughness.BaseColorG.Should().Be(0.0f);
        _ = material.PbrMetallicRoughness.BaseColorB.Should().Be(0.5f);
        _ = material.PbrMetallicRoughness.MetallicFactor.Should().Be(1.0f);
        _ = material.PbrMetallicRoughness.RoughnessFactor.Should().Be(0.0f);
        _ = material.NormalTexture!.Value.Scale.Should().Be(0.0f);
        _ = material.OcclusionTexture!.Value.Strength.Should().Be(1.0f);
    }

    [TestMethod]
    public void SourceWriter_ShouldWriteJsonThatSourceReaderCanRead()
    {
        var input = new MaterialSource(
            schema: "oxygen.material.v1",
            type: "PBR",
            name: "RoundTrip",
            pbrMetallicRoughness: new MaterialPbrMetallicRoughness(
                baseColorR: 0.25f,
                baseColorG: 0.5f,
                baseColorB: 0.75f,
                baseColorA: 1.0f,
                metallicFactor: 0.1f,
                roughnessFactor: 0.9f,
                baseColorTexture: new MaterialTextureRef("asset:///Content/Textures/BaseColor.png"),
                metallicRoughnessTexture: null),
            normalTexture: null,
            occlusionTexture: null,
            alphaMode: MaterialAlphaMode.Opaque,
            alphaCutoff: 0.5f,
            doubleSided: false);

        byte[] json;
        using (var ms = new MemoryStream())
        {
            MaterialSourceWriter.Write(ms, input);
            json = ms.ToArray();
        }

        var output = MaterialSourceReader.Read(json);

        _ = output.Schema.Should().Be("oxygen.material.v1");
        _ = output.Type.Should().Be("PBR");
        _ = output.Name.Should().Be("RoundTrip");
        _ = output.PbrMetallicRoughness.BaseColorR.Should().BeApproximately(0.25f, 0.0001f);
        _ = output.PbrMetallicRoughness.MetallicFactor.Should().BeApproximately(0.1f, 0.0001f);
        _ = output.PbrMetallicRoughness.RoughnessFactor.Should().BeApproximately(0.9f, 0.0001f);
        _ = output.PbrMetallicRoughness.BaseColorTexture!.Value.Source.Should().Be("asset:///Content/Textures/BaseColor.png");
    }

    private static float ReadSingle(byte[] bytes, int offset)
    {
        var bits = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(offset, 4));
        return BitConverter.Int32BitsToSingle((int)bits);
    }

    private static float ReadUnorm16(byte[] bytes, int offset)
        => BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(offset, 2)) / 65535.0f;

    private static string ReadNullTerminatedUtf8(ReadOnlySpan<byte> bytes)
    {
        var nullIndex = bytes.IndexOf((byte)0);
        if (nullIndex >= 0)
        {
            bytes = bytes[..nullIndex];
        }

        return Encoding.UTF8.GetString(bytes);
    }
}
