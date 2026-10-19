// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Tests;

[TestClass]
public sealed class MaterialSourceTests
{
    [TestMethod]
    public void GlossinessSourceExposesEffectiveRoughnessAndPreservesNativeRepresentation()
    {
        var source = MaterialSourceReader.Read("""{"parameters":{"roughness":0.125,"roughness_as_glossiness":true}}"""u8);
        _ = source.PbrMetallicRoughness.RoughnessFactor.Should().Be(0.875f);
        var unchanged = MaterialSourceWriter.ToJson(source with { DoubleSided = true });
        _ = unchanged["parameters"]!["roughness"]!.GetValue<float>().Should().Be(0.125f);
        var pbr = source.PbrMetallicRoughness;
        var edited = source with
        {
            PbrMetallicRoughness = new MaterialPbrMetallicRoughness(
                pbr.BaseColorR, pbr.BaseColorG, pbr.BaseColorB, pbr.BaseColorA,
                pbr.MetallicFactor, 0.25f),
        };
        var written = MaterialSourceWriter.ToJson(edited);
        _ = written["parameters"]!["roughness"]!.GetValue<float>().Should().Be(0.75f);
        _ = written["parameters"]!["roughness_as_glossiness"]!.GetValue<bool>().Should().BeTrue();
        var reread = MaterialSourceReader.Read(Encoding.UTF8.GetBytes(written.ToJsonString()));
        _ = reread.PbrMetallicRoughness.RoughnessFactor.Should().Be(0.25f);
    }

    [TestMethod]
    public void ScalarEditPreservesIndependentTexturesUvAndNativeShaderFields()
    {
        const string json = """
        {
          "name": "Coated",
          "domain": "masked",
          "alpha_mode": "masked",
          "orm_policy": "force_separate",
          "parameters": {
            "base_color": [0.2, 0.3, 0.4, 1],
            "metalness": 0.75, "roughness": 0.4,
            "ior": 1.7, "clearcoat_factor": 0.3,
            "emissive_color": [0.25, 0.5, 1], "emissive_intensity": 9.7
          },
          "textures": {
            "base_color": { "virtual_path": "/Art/Color.otex", "uv_set": 1,
              "uv_transform": { "scale": [2, 3], "offset": [0.1, 0.2], "rotation_radians": 0.5 } },
            "metallic": { "virtual_path": "/Art/Metal.otex" },
            "roughness": { "virtual_path": "/Art/Rough.otex" },
            "normal": { "virtual_path": "/Art/Normal.otex", "uv_set": 2 },
            "emissive": { "virtual_path": "/Art/Emit.otex", "uv_set": 3 }
          },
          "shaders": [{ "stage": "pixel", "source_path": "Coated.hlsl",
            "entry_point": "PSMain", "defines": "COATED=1", "shader_hash": 42 }]
        }
        """;
        var original = JsonNode.Parse(json)!;
        var source = MaterialSourceReader.Read(Encoding.UTF8.GetBytes(json));
        var edited = source with { AlphaCutoff = 0.25f };
        using var stream = new MemoryStream();
        MaterialSourceWriter.Write(stream, edited);
        var output = MaterialSourceReader.Read(stream.ToArray());
        var document = MaterialSourceWriter.ToJson(output);

        _ = output.AlphaCutoff.Should().Be(0.25f);
        _ = output.EmissiveIntensity.Should().Be(9.7f);
        _ = output.EmissiveIntensity.Should().NotBe((float)(Half)9.7f);
        _ = output.EmissiveColor.Should().Be(new Vector3(0.25f, 0.5f, 1f));
        _ = JsonNode.DeepEquals(document["textures"], original["textures"]).Should().BeTrue();
        _ = JsonNode.DeepEquals(document["shaders"], original["shaders"]).Should().BeTrue();
        _ = document["domain"]!.GetValue<string>().Should().Be("masked");
        _ = document["orm_policy"]!.GetValue<string>().Should().Be("force_separate");
        _ = document["parameters"]!["ior"]!.GetValue<float>().Should().Be(1.7f);
        _ = document["parameters"]!["clearcoat_factor"]!.GetValue<float>().Should().Be(0.3f);
    }

    [TestMethod]
    public void ZeroEmissionRetainsAuthoredColor()
    {
        var source = MaterialSourceReader.Read("""{"parameters":{"emissive_color":[0.25,0.5,0.75],"emissive_intensity":0}}"""u8);
        using var stream = new MemoryStream();
        MaterialSourceWriter.Write(stream, source);
        var output = MaterialSourceReader.Read(stream.ToArray());
        _ = output.EmissiveIntensity.Should().Be(0f);
        _ = output.EmissiveColor.Should().Be(new Vector3(0.25f, 0.5f, 0.75f));
    }

    [TestMethod]
    [DataRow("masked", MaterialAlphaMode.Mask)]
    [DataRow("alpha_blended", MaterialAlphaMode.Blend)]
    public void UnrelatedEditsPreserveDomainOnlySurfaceMode(string domain, MaterialAlphaMode mode)
    {
        var source = MaterialSourceReader.Read(Encoding.UTF8.GetBytes($$"""{"domain":"{{domain}}"}"""));
        _ = source.AlphaMode.Should().Be(mode);
        var unchanged = MaterialSourceWriter.ToJson(source with { AlphaCutoff = 0.3f });
        _ = unchanged["domain"]!.GetValue<string>().Should().Be(domain);
        _ = unchanged.ContainsKey("alpha_mode").Should().BeFalse();

        var opaque = MaterialSourceWriter.ToJson(source with { AlphaMode = MaterialAlphaMode.Opaque });
        _ = opaque["alpha_mode"]!.GetValue<string>().Should().Be("opaque");
        _ = opaque["domain"]!.GetValue<string>().Should().Be("opaque");
    }

    [TestMethod]
    public void SurfaceEditPreservesSpecializedDomain()
    {
        var source = MaterialSourceReader.Read("""{"domain":"decal","alpha_mode":"masked"}"""u8);
        var output = MaterialSourceWriter.ToJson(source with { AlphaMode = MaterialAlphaMode.Blend });
        _ = output["domain"]!.GetValue<string>().Should().Be("decal");
        _ = output["alpha_mode"]!.GetValue<string>().Should().Be("blended");
    }

    [TestMethod]
    public void SameImageWithIndependentUvBindingsKeepsBothBindings()
    {
        var source = MaterialSourceReader.Read("""
            { "textures": {
                "metallic": {"virtual_path":"/Art/Surface.otex","uv_set":0},
                "roughness": {"virtual_path":"/Art/Surface.otex","uv_set":1}
            } }
            """u8);
        _ = source.TextureReferences.Should().Contain("metallic", AssetPath.Parse("/Art/Surface.otex")).And.Contain("roughness", AssetPath.Parse("/Art/Surface.otex"));
        var output = MaterialSourceWriter.ToJson(source with { AlphaCutoff = 0.3f });
        _ = output["textures"]!["roughness"]!["uv_set"]!.GetValue<int>().Should().Be(1);
        _ = output["textures"]!["metallic"]!["uv_set"]!.GetValue<int>().Should().Be(0);
    }

    [TestMethod]
    public void TextureEditsChangeOnlyTheSelectedChannelAndPreserveItsUvBinding()
    {
        var source = MaterialSourceReader.Read("""
            { "textures": {
                "base_color": { "virtual_path": "/Art/Old.otex", "uv_set": 2,
                  "uv_transform": { "scale": [2, 3], "offset": [0.1, 0.2] } },
                "normal": { "virtual_path": "/Art/Normal.otex", "uv_set": 1 }
            } }
            """u8);

        var edited = source.WithTextureReference("base_color", AssetPath.Parse("/Content/Textures/New.otex"));
        var output = MaterialSourceWriter.ToJson(edited);

        _ = output["textures"]!["base_color"]!["virtual_path"]!.GetValue<string>().Should().Be("/Content/Textures/New.otex");
        _ = output["textures"]!["base_color"]!["uv_set"]!.GetValue<int>().Should().Be(2);
        _ = output["textures"]!["base_color"]!["uv_transform"]!["scale"]![0]!.GetValue<float>().Should().Be(2f);
        _ = output["textures"]!["normal"]!["virtual_path"]!.GetValue<string>().Should().Be("/Art/Normal.otex");

        var cleared = edited.WithTextureReference("normal", texture: null);
        var clearedJson = MaterialSourceWriter.ToJson(cleared);
        _ = clearedJson["textures"]!.AsObject().ContainsKey("normal").Should().BeFalse();
        _ = clearedJson["textures"]!["base_color"]!["uv_set"]!.GetValue<int>().Should().Be(2);
    }

    [TestMethod]
    [DataRow("{\"Schema\":\"oxygen.material.v1\",\"Type\":\"PBR\"}", MaterialSourceCodes.SchemaMismatch)]
    [DataRow("{\"parameters\":{\"metalness\":2}}", MaterialSourceCodes.SchemaMismatch)]
    [DataRow("{\"parameters\":{\"emissive_intensity\":65505}}", MaterialSourceCodes.SchemaMismatch)]
    [DataRow("{\"parameters\":{\"emissive_color\":[1,-1,0]}}", MaterialSourceCodes.SchemaMismatch)]
    [DataRow("{\"parameters\":{\"emissive_factor\":[1,1,1]}}", MaterialSourceCodes.SchemaMismatch)]
    [DataRow("{\"parameters\":{\"normal_scale\":1e100}}", MaterialSourceCodes.NumberOutOfRange)]
    [DataRow("{\"textures\":{\"normal\":{\"virtual_path\":\"asset:///Art/Normal.otex\"}}}", MaterialSourceCodes.TexturePathInvalid)]
    [DataRow("{\"textures\":{\"normal\":{\"virtual_path\":\"/Art/../Normal.otex\"}}}", MaterialSourceCodes.TexturePathInvalid)]
    [DataRow("{\"textures\":{\"normal\":{\"virtual_path\":\"/Normal.otex\"}}}", MaterialSourceCodes.TexturePathInvalid)]
    public void InvalidOrRetiredSourceIsRejectedWithoutClamping(string json, string code)
    {
        Action read = () => MaterialSourceReader.Read(Encoding.UTF8.GetBytes(json));
        _ = read.Should().Throw<AssetFormatException>().Which.Problem.Code.Should().Be(code);
    }

    [TestMethod]
    public void Read_NotJson_ReportsWhereTheErrorIs()
    {
        Action read = () => MaterialSourceReader.Read("{\n  \"name\": \"Bark\",\n  oops\n}"u8);

        var problem = read.Should().Throw<AssetFormatException>().Which.Problem;
        _ = problem.Code.Should().Be(MaterialSourceCodes.NotJson);
        _ = problem.Line.Should().Be(3);
        _ = problem.Column.Should().Be(3);
    }

    [TestMethod]
    public void NewSource_HasNoTextureAndUnitScales()
    {
        var source = new MaterialSource(
            name: null,
            new MaterialPbrMetallicRoughness(1, 1, 1, 1, metallicFactor: 0, roughnessFactor: 0.5f),
            MaterialAlphaMode.Opaque,
            alphaCutoff: 0.5f,
            doubleSided: false);

        _ = source.TextureReferences.Should().BeEmpty();
        _ = source.NormalScale.Should().Be(1.0f);
        _ = source.OcclusionStrength.Should().Be(1.0f);
    }

    [TestMethod]
    [DataRow("no_such_channel", "/Content/Textures/Bark.otex")]
    [DataRow("base_color", "/Bark.otex")]
    public void WithTextureReference_InvalidChannelOrMount_ThrowsAndKeepsTheSource(string channel, string texture)
    {
        var source = MaterialSourceReader.Read("""{"textures":{"base_color":{"virtual_path":"/Content/Textures/Oak.otex"}}}"""u8);

        var act = () => source.WithTextureReference(channel, AssetPath.Parse(texture));

        _ = act.Should().Throw<ArgumentException>();
        _ = source.TextureReferences.Should().Equal(new Dictionary<string, AssetPath>(StringComparer.Ordinal) { ["base_color"] = AssetPath.Parse("/Content/Textures/Oak.otex") });
    }

    [TestMethod]
    public void NormalScale_IsWrittenWhenANormalTextureIsAssigned()
    {
        var source = MaterialSourceReader.Read("{}"u8).WithTextureReference("normal", AssetPath.Parse("/Content/Textures/Normal.otex"));

        var written = MaterialSourceWriter.ToJson(source);

        _ = written["parameters"]!["normal_scale"]!.GetValue<float>().Should().Be(1.0f);
        _ = written["textures"]!["normal"]!["virtual_path"]!.GetValue<string>().Should().Be("/Content/Textures/Normal.otex");
    }
}
