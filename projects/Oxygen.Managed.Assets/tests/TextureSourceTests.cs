// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Managed.Assets.Authoring.Textures;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Tests;

[TestClass]
public sealed class TextureSourceTests
{
    private static readonly AssetPath Wood = AssetPath.Parse("/Content/Textures/Wood.otex");

    private static readonly FileSystemPath Project = FileSystemPath.From(Path.Combine(Path.GetTempPath(), "Project"));

    private static readonly MountFileLocator Locator = new(new Dictionary<string, FileSystemPath>(StringComparer.Ordinal)
    {
        ["Content"] = Project.Combine("Content"),
        ["RawMedia"] = Project.Combine("Media"),
    });

    [TestMethod]
    public void ReadNamesEveryImageTheCookerReads()
    {
        var texture = TextureSourceReader.Read(
            AuthoredFiles.Texture("/Content/Textures/Terrain.otex", "Grass.png")
                .Append("/sources", new JsonObject { ["file"] = "Rock.png", ["layer"] = 1 })
                .Append("/sources", new JsonObject { ["file"] = "../Shared/Snow.png", ["layer"] = 2, ["mip"] = 0 })
                .Bytes, Wood);

        _ = texture.Images.Should().Equal("Grass.png", "Rock.png", "../Shared/Snow.png");
        _ = texture.Sources[1].Should().Be(new TextureSourceMapping("../Shared/Snow.png") { Layer = 2, Mip = 0 });
    }

    [TestMethod]
    public void ImagesResolveAgainstTheTexturesIdentityAndInOtherMounts()
    {
        var texture = new TextureSource("Grass.png") { Sources = [new("../Shared/Snow.png"), new("/RawMedia/Sky.hdr")] };

        var images = texture.ResolveImages(AssetPath.Parse("/Content/Textures/Terrain.otex"), Locator, static _ => true);

        _ = images.Should().Equal(
            new TextureImageFile("Grass.png", Project.Combine("Content/Textures/Grass.png")),
            new TextureImageFile("../Shared/Snow.png", Project.Combine("Content/Shared/Snow.png")),
            new TextureImageFile("/RawMedia/Sky.hdr", Project.Combine("Media/Sky.hdr")));
    }

    [TestMethod]
    public void AnImageInAMountThatIsNotBoundNamesNoFile()
    {
        var images = new TextureSource("/Elsewhere/Sky.hdr").ResolveImages(Wood, Locator, static _ => true);

        _ = images.Should().Equal(new TextureImageFile("/Elsewhere/Sky.hdr", File: null));
    }

    [TestMethod]
    public void ASixFaceCubeInAMountThatIsNotBoundNamesNoFaces()
    {
        var texture = new TextureSource("/Elsewhere/Sky_px.hdr") { Cube = new TextureCubeSettings { Cubemap = true } };

        _ = texture.ResolveImages(Wood, Locator, static _ => true).Should().Equal(new TextureImageFile("/Elsewhere/Sky_px.hdr", File: null));
    }

    [TestMethod]
    [DataRow("Sky_px.hdr", new[] { "Sky_px.hdr", "Sky_nx.hdr", "Sky_py.hdr", "Sky_ny.hdr", "Sky_pz.hdr", "Sky_nz.hdr" })]
    [DataRow("Sky_posx.hdr", new[] { "Sky_posx.hdr", "Sky_negx.hdr", "Sky_posy.hdr", "Sky_negy.hdr", "Sky_posz.hdr", "Sky_negz.hdr" })]
    [DataRow("Sky.hdr", new[] { "Sky_right.hdr", "Sky_left.hdr", "Sky_top.hdr", "Sky_bottom.hdr", "Sky_front.hdr", "Sky_back.hdr" })]
    public void ASixFaceCubeReadsTheFirstCompleteSetOfFacesAsTheCookerFindsThem(string source, string[] existing)
    {
        var folder = Project.Combine("Content/Sky");
        var files = existing.Select(name => folder.Combine(name).FullPath).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var texture = new TextureSource(source) { Cube = new TextureCubeSettings { Cubemap = true } };

        var images = texture.ResolveImages(AssetPath.Parse("/Content/Sky/Sky.otex"), Locator, image => files.Contains(image.FullPath));

        _ = texture.IsSixFaceCube.Should().BeTrue();
        _ = images.Select(static image => image.File!.Name).Should().Equal(existing);
    }

    [TestMethod]
    public void ASixFaceCubeWithoutACompleteSetReadsTheFirstSetSoTheMissingFacesAreReported()
    {
        var texture = new TextureSource("Sky_px.hdr") { Cube = new TextureCubeSettings { Cubemap = true } };

        var images = texture.ResolveImages(AssetPath.Parse("/Content/Sky/Sky.otex"), Locator, static _ => false);

        _ = images.Select(static image => image.File!.Name).Should().Equal("Sky_px.hdr", "Sky_nx.hdr", "Sky_py.hdr", "Sky_ny.hdr", "Sky_pz.hdr", "Sky_nz.hdr");
    }

    [TestMethod]
    [DataRow("""{ "source": "Sky_px.hdr", "cube": { "cubemap": true, "cube_layout": "hcross" } }""")]
    [DataRow("""{ "source": "Sky_px.hdr", "cube": { "cubemap": true, "cube_layout": "auto" } }""")]
    [DataRow("""{ "source": "Sky_px.hdr", "cube": { "equirect_to_cube": true, "cube_face_size": 512 } }""")]
    [DataRow("""{ "source": "Sky_px.hdr", "sources": [ { "file": "Sky_nx.hdr", "layer": 1 } ], "cube": { "cubemap": true } }""")]
    [DataRow("""{ "source": "Sky_px.hdr" }""")]
    public void OnlyACubeWithoutLayoutProjectionOrOtherSourcesIsMadeOfSixFaceFiles(string json)
        => _ = TextureSourceReader.Read(Encoding.UTF8.GetBytes(json), Wood).IsSixFaceCube.Should().BeFalse();

    [TestMethod]
    [DataRow("""{ "source": "Sky.hdr", "output": { "format": "rgba16f" }, "cube": { "equirect_to_cube": true, "cube_face_size": 512 } }""", true, true)]
    [DataRow("""{ "source": "Sky.hdr", "output": { "format": "rgba32f" }, "cube": { "cubemap": true, "cube_layout": "hcross" } }""", true, true)]
    [DataRow("""{ "source": "Sky.png", "output": { "format": "rgba8_srgb" }, "cube": { "cube_layout": "hstrip" } }""", true, false)]
    [DataRow("""{ "source": "Sky.png", "cube": { "cubemap": false } }""", false, false)]
    [DataRow("""{ "source": "Sky.hdr", "output": { "format": "rgba16f" } }""", false, true)]
    public void CubeAndRadianceComeFromTheCubeAndOutputSettings(string json, bool isCube, bool storesRadiance)
    {
        var texture = TextureSourceReader.Read(Encoding.UTF8.GetBytes(json), Wood);

        _ = texture.IsCube.Should().Be(isCube);
        _ = texture.StoresRadiance.Should().Be(storesRadiance);
    }

    [TestMethod]
    [DataRow("""{ "source": "Wood.png" """, TextureSourceCodes.NotJson)]
    [DataRow("""{ "virtual_path": "/Content/Textures/Wood.otex" }""", TextureSourceCodes.SchemaMismatch)]
    [DataRow("""{ "source": "Wood.png", "mips": { "policy": "max" } }""", TextureSourceCodes.SchemaMismatch)]
    [DataRow("""{ "source": "Wood.png", "virtual_path": "/Content/Textures/Wood.otex" }""", TextureSourceCodes.SchemaMismatch)]
    [DataRow("""{ "source": "../../../Wood.png" }""", "vpath.reference_outside_mount")]
    [DataRow("""{ "source": "/Content/Wood.png" }""", "vpath.reference_self_absolute")]
    [DataRow("""{ "source": "Wood.png", "sources": [ { "file": "../../../Other.png", "layer": 1 } ] }""", "vpath.reference_outside_mount")]
    public void ReadRefusesWhatTheCookerRefuses(string json, string code)
    {
        var read = () => TextureSourceReader.Read(Encoding.UTF8.GetBytes(json), Wood);

        _ = read.Should().Throw<AssetFormatException>().Which.Problem.Code.Should().Be(code);
    }

    [TestMethod]
    public void ReadAcceptsAByteOrderMark()
        => _ = TextureSourceReader.Read([.. Encoding.UTF8.Preamble, .. AuthoredFiles.Texture("/Content/Textures/Wood.otex", "Wood.png").Bytes], Wood).Source.Should().Be("Wood.png");

    [TestMethod]
    public void WriteKeepsTheFieldsTheModelDoesNotName()
    {
        var texture = TextureSourceReader.Read(
            AuthoredFiles.Texture("/Content/Textures/Wood.otex", "Wood.png")
                .Set("/name", "Wood")
                .Set("/intent", "albedo")
                .Set("/decode", JsonNode.Parse("""{ "color_space": "srgb", "flip_y": true }"""))
                .Set("/mips", JsonNode.Parse("""{ "policy": "full", "filter": "kaiser" }"""))
                .Set("/output", JsonNode.Parse("""{ "format": "bc7_srgb", "bc7_quality": "high" }"""))
                .Bytes, Wood);

        var written = TextureSourceWriter.ToJson(texture with { Source = "../Wood.png", Format = "rgba8_srgb", ColorSpace = null }, Wood);

        _ = written["source"]!.GetValue<string>().Should().Be("../Wood.png");
        _ = written["output"]!["format"]!.GetValue<string>().Should().Be("rgba8_srgb");
        _ = written["output"]!["bc7_quality"]!.GetValue<string>().Should().Be("high");
        _ = written["mips"]!["filter"]!.GetValue<string>().Should().Be("kaiser");
        _ = written["decode"]!.AsObject().ContainsKey("color_space").Should().BeFalse();
        _ = written["decode"]!["flip_y"]!.GetValue<bool>().Should().BeTrue();
    }

    [TestMethod]
    public void WriteRoundTripsANewTexture()
    {
        var texture = new TextureSource("Sky_px.hdr")
        {
            Name = "Sky",
            Sources = [new("Sky_mip1.hdr") { Mip = 1 }],
            Intent = "hdr_env",
            ColorSpace = "linear",
            Format = "rgba16f",
            Cube = new() { Cubemap = true },
        };

        var reread = TextureSourceReader.Read(Encoding.UTF8.GetBytes(TextureSourceWriter.ToJson(texture, Wood).ToJsonString()), Wood);

        _ = reread.Name.Should().Be("Sky");
        _ = reread.Images.Should().Equal("Sky_px.hdr", "Sky_mip1.hdr");
        _ = reread.Sources.Single().Mip.Should().Be(1);
        _ = reread.Intent.Should().Be("hdr_env");
        _ = reread.ColorSpace.Should().Be("linear");
        _ = reread.Format.Should().Be("rgba16f");
        _ = reread.Cube.Should().Be(new TextureCubeSettings { Cubemap = true });
    }

    [TestMethod]
    public void WriteDropsTheCubeOfATextureThatIsNoLongerOne()
    {
        var texture = TextureSourceReader.Read(
            AuthoredFiles.Texture("/Content/Textures/Sky.otex", "Sky.hdr")
                .Set("/cube", JsonNode.Parse("""{ "equirect_to_cube": true, "cube_face_size": 256 }"""))
                .Bytes, Wood);

        _ = TextureSourceWriter.ToJson(texture with { Cube = null }, Wood).ContainsKey("cube").Should().BeFalse();
    }

    [TestMethod]
    public void WriteRefusesSettingsTheCookerRefuses()
    {
        var write = () => TextureSourceWriter.ToJson(new TextureSource("Wood.png") { Format = "dxt1" }, Wood);

        _ = write.Should().Throw<AssetFormatException>().Which.Problem.Code.Should().Be(TextureSourceCodes.SchemaMismatch);
    }
}
