// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using AwesomeAssertions;
using Oxygen.Managed.Assets.Authoring.Textures;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Tests;

[TestClass]
public sealed class TextureSourceTests
{
    [TestMethod]
    public void ReadNamesEveryImageTheCookerReads()
    {
        var texture = TextureSourceReader.Read("""
            {
              "virtual_path": "/Content/Textures/Terrain.otex",
              "source": "Grass.png",
              "sources": [ { "file": "Rock.png", "layer": 1 }, { "file": "../Shared/Snow.png", "layer": 2, "mip": 0 } ]
            }
            """u8);

        _ = texture.Images.Should().Equal("Grass.png", "Rock.png", "../Shared/Snow.png");
        _ = texture.Sources[1].Should().Be(new TextureSourceMapping("../Shared/Snow.png") { Layer = 2, Mip = 0 });
        _ = texture.VirtualPath.Should().Be(AssetPath.Parse("/Content/Textures/Terrain.otex"));
    }

    [TestMethod]
    public void ImagesResolveAgainstTheDescriptorFolder()
    {
        var folder = Path.Combine(Path.GetTempPath(), "Project", "Content", "Textures");
        var rooted = Path.Combine(Path.GetTempPath(), "Elsewhere", "Sky.hdr");
        var texture = new TextureSource("Grass.png") { Sources = [new("../Shared/Snow.png"), new(rooted)] };

        var images = texture.ResolveImages(FileSystemPath.From(Path.Combine(folder, "Terrain.otex.json")));

        _ = images.Select(static image => image.FullPath).Should().Equal(
            Path.Combine(folder, "Grass.png"),
            Path.GetFullPath(Path.Combine(folder, "..", "Shared", "Snow.png")),
            rooted);
    }

    [TestMethod]
    [DataRow("""{ "source": "Sky.hdr", "output": { "format": "rgba16f" }, "cube": { "equirect_to_cube": true, "cube_face_size": 512 } }""", true, true)]
    [DataRow("""{ "source": "Sky.hdr", "output": { "format": "rgba32f" }, "cube": { "cubemap": true, "cube_layout": "hcross" } }""", true, true)]
    [DataRow("""{ "source": "Sky.png", "output": { "format": "rgba8_srgb" }, "cube": { "cube_layout": "hstrip" } }""", true, false)]
    [DataRow("""{ "source": "Sky.png", "cube": { "cubemap": false } }""", false, false)]
    [DataRow("""{ "source": "Sky.hdr", "output": { "format": "rgba16f" } }""", false, true)]
    public void CubeAndRadianceComeFromTheCubeAndOutputSettings(string json, bool isCube, bool storesRadiance)
    {
        var texture = TextureSourceReader.Read(Encoding.UTF8.GetBytes(json));

        _ = texture.IsCube.Should().Be(isCube);
        _ = texture.StoresRadiance.Should().Be(storesRadiance);
    }

    [TestMethod]
    [DataRow("""{ "source": "Wood.png" """, TextureSourceCodes.NotJson)]
    [DataRow("""{ "virtual_path": "/Content/Textures/Wood.otex" }""", TextureSourceCodes.SchemaMismatch)]
    [DataRow("""{ "source": "Wood.png", "mips": { "policy": "max" } }""", TextureSourceCodes.SchemaMismatch)]
    [DataRow("""{ "source": "Wood.png", "virtual_path": "/Content/Textures/../Wood.otex" }""", TextureSourceCodes.VirtualPathInvalid)]
    public void ReadRefusesWhatTheCookerRefuses(string json, string code)
    {
        var read = () => TextureSourceReader.Read(Encoding.UTF8.GetBytes(json));

        _ = read.Should().Throw<AssetFormatException>().Which.Problem.Code.Should().Be(code);
    }

    [TestMethod]
    public void ReadAcceptsAByteOrderMark()
        => _ = TextureSourceReader.Read([.. Encoding.UTF8.Preamble, .. """{ "source": "Wood.png" }"""u8]).Source.Should().Be("Wood.png");

    [TestMethod]
    public void WriteKeepsTheFieldsTheModelDoesNotName()
    {
        var texture = TextureSourceReader.Read("""
            {
              "name": "Wood",
              "source": "Wood.png",
              "intent": "albedo",
              "decode": { "color_space": "srgb", "flip_y": true },
              "mips": { "policy": "full", "filter": "kaiser" },
              "output": { "format": "bc7_srgb", "bc7_quality": "high" }
            }
            """u8);

        var written = TextureSourceWriter.ToJson(texture with { Source = "../Wood.png", Format = "rgba8_srgb", ColorSpace = null });

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
            VirtualPath = AssetPath.Parse("/Content/Textures/Sky.otex"),
            Sources = [new("Sky_mip1.hdr") { Mip = 1 }],
            Intent = "hdr_env",
            ColorSpace = "linear",
            Format = "rgba16f",
            Cube = new() { Cubemap = true },
        };

        var reread = TextureSourceReader.Read(Encoding.UTF8.GetBytes(TextureSourceWriter.ToJson(texture).ToJsonString()));

        _ = reread.Name.Should().Be("Sky");
        _ = reread.VirtualPath.Should().Be(texture.VirtualPath);
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
        var texture = TextureSourceReader.Read("""{ "source": "Sky.hdr", "cube": { "equirect_to_cube": true, "cube_face_size": 256 } }"""u8);

        _ = TextureSourceWriter.ToJson(texture with { Cube = null }).ContainsKey("cube").Should().BeFalse();
    }

    [TestMethod]
    public void WriteRefusesSettingsTheCookerRefuses()
    {
        var write = () => TextureSourceWriter.ToJson(new TextureSource("Wood.png") { Format = "dxt1" });

        _ = write.Should().Throw<AssetFormatException>().Which.Problem.Code.Should().Be(TextureSourceCodes.SchemaMismatch);
    }
}
