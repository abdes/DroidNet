// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Tests;

[TestClass]
public sealed class MaterialSourceTests
{
    private static readonly AssetPath Anchor = AssetPath.Parse("/Content/Materials/Anchor.omat");

    [TestMethod]
    public void GlossinessSourceExposesEffectiveRoughnessAndPreservesNativeRepresentation()
    {
        var source = MaterialSourceReader.Read(
            AuthoredFiles.Material("/Content/Materials/Glossy.omat")
                .Set("/parameters/roughness", 0.125)
                .Set("/parameters/roughness_as_glossiness", true)
                .Bytes, Anchor);
        _ = source.PbrMetallicRoughness.RoughnessFactor.Should().Be(0.875f);
        var unchanged = MaterialSourceWriter.ToJson(source with { DoubleSided = true }, Anchor);
        _ = unchanged["parameters"]!["roughness"]!.GetValue<float>().Should().Be(0.125f);
        var pbr = source.PbrMetallicRoughness;
        var edited = source with
        {
            PbrMetallicRoughness = new MaterialPbrMetallicRoughness(
                pbr.BaseColorR, pbr.BaseColorG, pbr.BaseColorB, pbr.BaseColorA,
                pbr.MetallicFactor, 0.25f),
        };
        var written = MaterialSourceWriter.ToJson(edited, Anchor);
        _ = written["parameters"]!["roughness"]!.GetValue<float>().Should().Be(0.75f);
        _ = written["parameters"]!["roughness_as_glossiness"]!.GetValue<bool>().Should().BeTrue();
        var reread = MaterialSourceReader.Read(Encoding.UTF8.GetBytes(written.ToJsonString()), Anchor);
        _ = reread.PbrMetallicRoughness.RoughnessFactor.Should().Be(0.25f);
    }

    [TestMethod]
    public void ScalarEditPreservesIndependentTexturesUvAndNativeShaderFields()
    {
        var material = AuthoredFiles.Material("/Content/Materials/Coated.omat")
            .Set("/domain", "masked")
            .Set("/alpha_mode", "masked")
            .Set("/orm_policy", "force_separate")
            .Set("/parameters", JsonNode.Parse("""
                {
                  "base_color": [0.2, 0.3, 0.4, 1],
                  "metalness": 0.75, "roughness": 0.4,
                  "ior": 1.7, "clearcoat_factor": 0.3,
                  "emissive_color": [0.25, 0.5, 1], "emissive_intensity": 9.7
                }
                """))
            .Texture("base_color", "/Art/Color.otex")
            .Set("/textures/base_color/uv_set", 1)
            .Set("/textures/base_color/uv_transform", JsonNode.Parse("""{ "scale": [2, 3], "offset": [0.1, 0.2], "rotation_radians": 0.5 }"""))
            .Texture("metallic", "/Art/Metal.otex")
            .Texture("roughness", "/Art/Rough.otex")
            .Texture("normal", "/Art/Normal.otex")
            .Set("/textures/normal/uv_set", 1)
            .Texture("emissive", "/Art/Emit.otex")
            .Set("/textures/emissive/uv_set", 1)
            .Set("/shaders", JsonNode.Parse("""
                [{ "stage": "pixel", "source_path": "Coated.hlsl",
                  "entry_point": "PSMain", "defines": "COATED=1", "shader_hash": 42 }]
                """));
        var original = material.Json;
        var source = MaterialSourceReader.Read(material.Bytes, Anchor);
        var edited = source with { AlphaCutoff = 0.25f };
        using var stream = new MemoryStream();
        MaterialSourceWriter.Write(stream, edited, Anchor);
        var output = MaterialSourceReader.Read(stream.ToArray(), Anchor);
        var document = MaterialSourceWriter.ToJson(output, Anchor);

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
        var source = MaterialSourceReader.Read(
            AuthoredFiles.Material("/Content/Materials/Dark.omat")
                .Set("/parameters/emissive_color", JsonNode.Parse("[0.25, 0.5, 0.75]"))
                .Set("/parameters/emissive_intensity", 0)
                .Bytes, Anchor);
        using var stream = new MemoryStream();
        MaterialSourceWriter.Write(stream, source, Anchor);
        var output = MaterialSourceReader.Read(stream.ToArray(), Anchor);
        _ = output.EmissiveIntensity.Should().Be(0f);
        _ = output.EmissiveColor.Should().Be(new Vector3(0.25f, 0.5f, 0.75f));
    }

    [TestMethod]
    [DataRow("masked", MaterialAlphaMode.Mask)]
    [DataRow("alpha_blended", MaterialAlphaMode.Blend)]
    public void UnrelatedEditsPreserveDomainOnlySurfaceMode(string domain, MaterialAlphaMode mode)
    {
        var source = MaterialSourceReader.Read(AuthoredFiles.Material("/Content/Materials/Surface.omat").Set("/domain", domain).Bytes, Anchor);
        _ = source.AlphaMode.Should().Be(mode);
        var unchanged = MaterialSourceWriter.ToJson(source with { AlphaCutoff = 0.3f }, Anchor);
        _ = unchanged["domain"]!.GetValue<string>().Should().Be(domain);
        _ = unchanged.ContainsKey("alpha_mode").Should().BeFalse();

        var opaque = MaterialSourceWriter.ToJson(source with { AlphaMode = MaterialAlphaMode.Opaque }, Anchor);
        _ = opaque["alpha_mode"]!.GetValue<string>().Should().Be("opaque");
        _ = opaque["domain"]!.GetValue<string>().Should().Be("opaque");
    }

    [TestMethod]
    public void SurfaceEditPreservesSpecializedDomain()
    {
        var source = MaterialSourceReader.Read(
            AuthoredFiles.Material("/Content/Materials/Stamp.omat").Set("/domain", "decal").Set("/alpha_mode", "masked").Bytes, Anchor);
        var output = MaterialSourceWriter.ToJson(source with { AlphaMode = MaterialAlphaMode.Blend }, Anchor);
        _ = output["domain"]!.GetValue<string>().Should().Be("decal");
        _ = output["alpha_mode"]!.GetValue<string>().Should().Be("blended");
    }

    [TestMethod]
    public void SameImageWithIndependentUvBindingsKeepsBothBindings()
    {
        var source = MaterialSourceReader.Read(
            AuthoredFiles.Material("/Content/Materials/Packed.omat")
                .Texture("metallic", "/Art/Surface.otex")
                .Set("/textures/metallic/uv_set", 0)
                .Texture("roughness", "/Art/Surface.otex")
                .Set("/textures/roughness/uv_set", 1)
                .Bytes, Anchor);
        _ = source.TextureReferences.Should().Contain("metallic", AssetPath.Parse("/Art/Surface.otex")).And.Contain("roughness", AssetPath.Parse("/Art/Surface.otex"));
        var output = MaterialSourceWriter.ToJson(source with { AlphaCutoff = 0.3f }, Anchor);
        _ = output["textures"]!["roughness"]!["uv_set"]!.GetValue<int>().Should().Be(1);
        _ = output["textures"]!["metallic"]!.AsObject().ContainsKey("uv_set").Should().BeFalse("set 0 is the default");
    }

    [TestMethod]
    public void TextureEditsChangeOnlyTheSelectedChannelAndPreserveItsUvBinding()
    {
        var source = MaterialSourceReader.Read(
            AuthoredFiles.Material("/Content/Materials/Bark.omat")
                .Texture("base_color", "/Art/Old.otex")
                .Set("/textures/base_color/uv_set", 1)
                .Set("/textures/base_color/uv_transform", JsonNode.Parse("""{ "scale": [2, 3], "offset": [0.1, 0.2] }"""))
                .Texture("normal", "/Art/Normal.otex")
                .Set("/textures/normal/uv_set", 1)
                .Bytes, Anchor);

        var edited = source.WithTextureReference("base_color", AssetPath.Parse("/Content/Textures/New.otex"));
        var output = MaterialSourceWriter.ToJson(edited, Anchor);

        _ = output["textures"]!["base_color"]!["virtual_path"]!.GetValue<string>().Should().Be("../Textures/New.otex");
        _ = output["textures"]!["base_color"]!["uv_set"]!.GetValue<int>().Should().Be(1);
        _ = output["textures"]!["base_color"]!["uv_transform"]!["scale"]![0]!.GetValue<float>().Should().Be(2f);
        _ = output["textures"]!["normal"]!["virtual_path"]!.GetValue<string>().Should().Be("/Art/Normal.otex");

        var cleared = edited.WithTextureReference("normal", texture: null);
        var clearedJson = MaterialSourceWriter.ToJson(cleared, Anchor);
        _ = clearedJson["textures"]!.AsObject().ContainsKey("normal").Should().BeFalse();
        _ = clearedJson["textures"]!["base_color"]!["uv_set"]!.GetValue<int>().Should().Be(1);
    }

    [TestMethod]
    [DataRow("{\"Schema\":\"oxygen.material.v1\",\"Type\":\"PBR\"}", MaterialSourceCodes.SchemaMismatch)]
    [DataRow("{\"parameters\":{\"metalness\":2}}", MaterialSourceCodes.SchemaMismatch)]
    [DataRow("{\"parameters\":{\"emissive_intensity\":65505}}", MaterialSourceCodes.SchemaMismatch)]
    [DataRow("{\"parameters\":{\"emissive_color\":[1,-1,0]}}", MaterialSourceCodes.SchemaMismatch)]
    [DataRow("{\"parameters\":{\"emissive_factor\":[1,1,1]}}", MaterialSourceCodes.SchemaMismatch)]
    [DataRow("{\"parameters\":{\"normal_scale\":1e100}}", MaterialSourceCodes.SchemaMismatch)]
    [DataRow("{\"textures\":{\"normal\":{\"virtual_path\":\"asset:///Art/Normal.otex\"}}}", MaterialSourceCodes.SchemaMismatch)]
    [DataRow("{\"textures\":{\"normal\":{\"virtual_path\":\"/Art/../Normal.otex\"}}}", MaterialSourceCodes.SchemaMismatch)]
    [DataRow("{\"textures\":{\"normal\":{\"virtual_path\":\"/Normal.otex\"}}}", MaterialSourceCodes.SchemaMismatch)]
    public void InvalidOrRetiredSourceIsRejectedWithoutClamping(string json, string code)
    {
        Action read = () => MaterialSourceReader.Read(Encoding.UTF8.GetBytes(json), Anchor);
        _ = read.Should().Throw<AssetFormatException>().Which.Problem.Code.Should().Be(code);
    }

    [TestMethod]
    [DataRow("../../../Normal.otex", "vpath.reference_outside_mount")]
    [DataRow("/Content/Textures/Normal.otex", "vpath.reference_self_absolute")]
    public void AReferenceThatDoesNotResolveIsRefusedWithItsCode(string reference, string code)
    {
        var json = JsonSerializer.SerializeToUtf8Bytes(new JsonObject { ["textures"] = new JsonObject { ["normal"] = new JsonObject { ["virtual_path"] = reference } } });

        var read = () => MaterialSourceReader.Read(json, Anchor);

        _ = read.Should().Throw<AssetFormatException>().Which.Problem.Code.Should().Be(code);
    }

    [TestMethod]
    [DataRow("Wood.otex", "/Content/Materials/Wood.otex")]
    [DataRow("./Wood.otex", "/Content/Materials/Wood.otex")]
    [DataRow("../Textures/Wood.otex", "/Content/Textures/Wood.otex")]
    [DataRow("/Art/Wood.otex", "/Art/Wood.otex")]
    public void AReferenceResolvesAgainstTheMaterialsIdentity(string reference, string texture)
    {
        var json = JsonSerializer.SerializeToUtf8Bytes(new JsonObject { ["textures"] = new JsonObject { ["normal"] = new JsonObject { ["virtual_path"] = reference } } });

        _ = MaterialSourceReader.Read(json, Anchor).TextureReferences["normal"].Should().Be(AssetPath.Parse(texture));
    }

    [TestMethod]
    public void AReferenceIsSpelledFromTheIdentityWritten()
    {
        var source = MaterialSourceReader.Read(AuthoredFiles.Material(Anchor.VirtualPath).Bytes, Anchor)
            .WithTextureReference("base_color", AssetPath.Parse("/Content/Textures/Wood.otex"));

        string Spelled(string identity) => MaterialSourceWriter.ToJson(source, AssetPath.Parse(identity))["textures"]!["base_color"]!["virtual_path"]!.GetValue<string>();

        _ = Spelled("/Content/Textures/Wall.omat").Should().Be("Wood.otex");
        _ = Spelled("/Content/Deep/Folder/Wall.omat").Should().Be("../../Textures/Wood.otex");
        _ = Spelled("/Props/Wall.omat").Should().Be("/Content/Textures/Wood.otex");
    }

    [TestMethod]
    public void EveryBindingFieldRoundTrips()
    {
        var binding = new MaterialTextureBinding(AssetPath.Parse("/Art/Wood.otex"))
        {
            UvSet = 1,
            UvTransform = new MaterialUvTransform(new Vector2(2, 3), new Vector2(0.25f, 0.5f), 0.5f),
            Sampler = new MaterialTextureSampler
            {
                AddressU = MaterialTextureAddress.Clamp,
                AddressV = MaterialTextureAddress.Mirror,
                MagFilter = MaterialTextureFilter.Nearest,
                MinFilter = MaterialTextureFilter.Nearest,
                MipFilter = MaterialTextureMipFilter.None,
            },
        };
        var source = MaterialSourceReader.Read(AuthoredFiles.Material(Anchor.VirtualPath).Bytes, Anchor).WithTextureBinding("emissive", binding);

        using var stream = new MemoryStream();
        MaterialSourceWriter.Write(stream, source, Anchor);

        _ = MaterialSourceReader.Read(stream.ToArray(), Anchor).TextureBindings["emissive"].Should().Be(binding);
    }

    [TestMethod]
    public void ABindingFieldHoldingItsDefaultIsNotWritten()
    {
        var material = AuthoredFiles.Material(Anchor.VirtualPath)
            .Texture("base_color", "/Art/Wood.otex")
            .Set("/textures/base_color/uv_set", 0)
            .Set("/textures/base_color/uv_transform", JsonNode.Parse("""{ "scale": [1, 1], "offset": [0, 0], "rotation_radians": 0 }"""))
            .Set("/textures/base_color/sampler", JsonNode.Parse("""{ "address_u": "repeat", "mag_filter": "linear", "mip_filter": "linear" }"""));

        var written = MaterialSourceWriter.ToJson(MaterialSourceReader.Read(material.Bytes, Anchor), Anchor);

        _ = written["textures"]!["base_color"]!.AsObject().Select(static field => field.Key).Should().Equal("virtual_path");
    }

    [TestMethod]
    public void OnlyTheSamplerFieldsThatDifferAreWritten()
    {
        var source = MaterialSourceReader.Read(AuthoredFiles.Material(Anchor.VirtualPath).Bytes, Anchor)
            .WithTextureBinding("base_color", new MaterialTextureBinding(AssetPath.Parse("/Art/Wood.otex")) { Sampler = new MaterialTextureSampler { AddressV = MaterialTextureAddress.Clamp } });

        var sampler = MaterialSourceWriter.ToJson(source, Anchor)["textures"]!["base_color"]!["sampler"]!.AsObject();

        _ = sampler.Select(static field => field.Key).Should().Equal("address_v");
        _ = sampler["address_v"]!.GetValue<string>().Should().Be("clamp");
    }

    [TestMethod]
    public void AUvSetPastTheSecondIsRefused()
    {
        var read = () => MaterialSourceReader.Read(AuthoredFiles.Material(Anchor.VirtualPath).Texture("base_color", "/Art/Wood.otex").Set("/textures/base_color/uv_set", 2).Bytes, Anchor);
        var bind = () => MaterialSourceReader.Read(AuthoredFiles.Material(Anchor.VirtualPath).Bytes, Anchor)
            .WithTextureBinding("base_color", new MaterialTextureBinding(AssetPath.Parse("/Art/Wood.otex")) { UvSet = 2 });

        _ = read.Should().Throw<AssetFormatException>().Which.Problem.Code.Should().Be(MaterialSourceCodes.SchemaMismatch);
        _ = bind.Should().Throw<ArgumentOutOfRangeException>();
    }

    [TestMethod]
    public void TheOcclusionModeRoundTripsAndItsDefaultIsNotWritten()
    {
        var strength = MaterialSourceReader.Read(AuthoredFiles.Material(Anchor.VirtualPath).Set("/ambient_occlusion_mode", "strength").Bytes, Anchor);

        _ = strength.AmbientOcclusionMode.Should().Be(MaterialAmbientOcclusionMode.Strength);
        _ = MaterialSourceWriter.ToJson(strength, Anchor)["ambient_occlusion_mode"]!.GetValue<string>().Should().Be("strength");
        _ = MaterialSourceWriter.ToJson(strength with { AmbientOcclusionMode = MaterialAmbientOcclusionMode.Factor }, Anchor).ContainsKey("ambient_occlusion_mode").Should().BeFalse();
    }

    [TestMethod]
    public void AProceduralGridRoundTripsWithOnlyWhatItStates()
    {
        var grid = new MaterialProceduralGrid
        {
            Spacing = new Vector2(2, 4),
            MajorEvery = 5,
            LineThickness = 0.5f,
            FadeEnd = 100,
            MinorColor = new Vector4(0.25f, 0.5f, 0.75f, 1),
        };
        var source = MaterialSourceReader.Read(AuthoredFiles.Material(Anchor.VirtualPath).Bytes, Anchor) with { ProceduralGrid = grid };

        var written = MaterialSourceWriter.ToJson(source, Anchor);

        _ = written["procedural_grid"]!.AsObject().Select(static field => field.Key).Should().Equal("spacing", "major_every", "line_thickness", "fade_end", "minor_color");
        _ = MaterialSourceReader.Read(JsonSerializer.SerializeToUtf8Bytes(written), Anchor).ProceduralGrid.Should().Be(grid);
        _ = MaterialSourceWriter.ToJson(source with { ProceduralGrid = null }, Anchor).ContainsKey("procedural_grid").Should().BeFalse();
    }

    [TestMethod]
    public void AnEmptyProceduralGridStillTurnsTheGridOn()
    {
        var source = MaterialSourceReader.Read(AuthoredFiles.Material(Anchor.VirtualPath).Set("/procedural_grid", new JsonObject()).Bytes, Anchor);

        _ = source.ProceduralGrid.Should().Be(new MaterialProceduralGrid());
        _ = MaterialSourceWriter.ToJson(source, Anchor)["procedural_grid"]!.AsObject().Should().BeEmpty();
    }

    [TestMethod]
    public void AMaterialWithoutAlphaModeOrDomainIsOpaque()
        => _ = MaterialSourceReader.Read(AuthoredFiles.Material(Anchor.VirtualPath).Bytes, Anchor).AlphaMode.Should().Be(MaterialAlphaMode.Opaque);

    [TestMethod]
    public void Read_NotJson_ReportsWhereTheErrorIs()
    {
        Action read = () => MaterialSourceReader.Read("{\n  \"name\": \"Bark\",\n  oops\n}"u8, Anchor);

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
        var source = MaterialSourceReader.Read(
            AuthoredFiles.Material("/Content/Materials/Oak.omat").Texture("base_color", "/Content/Textures/Oak.otex").Bytes, Anchor);

        var act = () => source.WithTextureReference(channel, AssetPath.Parse(texture));

        _ = act.Should().Throw<ArgumentException>();
        _ = source.TextureReferences.Should().Equal(new Dictionary<string, AssetPath>(StringComparer.Ordinal) { ["base_color"] = AssetPath.Parse("/Content/Textures/Oak.otex") });
    }

    [TestMethod]
    public void NormalScale_IsWrittenWhenANormalTextureIsAssigned()
    {
        var source = MaterialSourceReader.Read(AuthoredFiles.Material("/Content/Materials/Plain.omat").Bytes, Anchor).WithTextureReference("normal", AssetPath.Parse("/Content/Textures/Normal.otex"));

        var written = MaterialSourceWriter.ToJson(source, Anchor);

        _ = written["parameters"]!["normal_scale"]!.GetValue<float>().Should().Be(1.0f);
        _ = written["textures"]!["normal"]!["virtual_path"]!.GetValue<string>().Should().Be("../Textures/Normal.otex");
    }
}
