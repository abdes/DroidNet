// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Textures.Tests;

/// <summary>
/// Importing an image copies it into an authoring folder and writes its descriptor beside it; a refused request says
/// why with a <see cref="TextureImportCodes"/> code and leaves no file behind.
/// </summary>
[TestClass]
public sealed class TextureImportTests
{
    // The user picks images anywhere on disk, outside the project.
    private const string Outside = @"C:\Outside";

    private readonly MockFileSystem fileSystem = new();
    private readonly TestProject project;
    private readonly TextureImport import;

    public TextureImportTests()
    {
        this.project = TestProject.Create(this.fileSystem);
        this.import = new TextureImport(this.fileSystem);
        _ = this.fileSystem.Directory.CreateDirectory(Outside);
    }

    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task AnImportCopiesTheImageAndWritesTheNamedDescriptorWithTheReviewedSettings()
    {
        var image = this.WriteOutside("wood.PNG", [1, 2, 3]);

        var texture = await this.import.CreateAsync(this.Request(image, "/Content/Textures", "Wood"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = texture.Should().Be(AssetPath.Parse("/Content/Textures/Wood.otex"), "the import names the texture, not its descriptor file");
        _ = (await this.fileSystem.File.ReadAllBytesAsync(this.project.PathOf("/Content/Textures/Wood.png").FullPath, this.TestContext.CancellationToken).ConfigureAwait(true)).Should().Equal([1, 2, 3]);
        using var descriptor = this.ReadJson("/Content/Textures/Wood.otex.json");
        _ = descriptor.RootElement.GetProperty("source").GetString().Should().Be("Wood.png");
        _ = descriptor.RootElement.GetProperty("virtual_path").GetString().Should().Be("/Content/Textures/Wood.otex");
        _ = descriptor.RootElement.GetProperty("intent").GetString().Should().Be("albedo");
        _ = descriptor.RootElement.GetProperty("decode").GetProperty("color_space").GetString().Should().Be("srgb");
        _ = descriptor.RootElement.GetProperty("output").GetProperty("format").GetString().Should().Be("rgba8_srgb");
    }

    [TestMethod]
    public async Task ANameInUseIsRefusedWithoutReplacingTheExistingTexture()
    {
        var image = this.WriteOutside("wood.tga", [9, 8, 7]);
        var existing = this.project.WriteFile("/Content/Textures/Wood.otex.json", "owned");

        var refusal = await RefusalOf(() => this.import.CreateAsync(this.Request(image, "/Content/Textures", "Wood"), this.TestContext.CancellationToken)).ConfigureAwait(false);

        _ = refusal.Should().Be(TextureImportCodes.NameTaken);
        _ = (await this.fileSystem.File.ReadAllTextAsync(existing.FullPath, this.TestContext.CancellationToken).ConfigureAwait(true)).Should().Be("owned");
        _ = this.fileSystem.Directory.GetFiles(existing.Parent!.FullPath).Should().ContainSingle();
    }

    [TestMethod]
    public async Task AMissingImageIsRefusedAndCanBeImportedOnceItExists()
    {
        var image = Path.Combine(Outside, "recovered.tga");
        var request = this.Request(image, "/Content/Textures", "Recovered") with { Intent = "normal", ColorSpace = "linear", Format = "bc7" };

        var refusal = await RefusalOf(() => this.import.CreateAsync(request, this.TestContext.CancellationToken)).ConfigureAwait(false);

        _ = refusal.Should().Be(TextureImportCodes.ImageMissing);
        _ = this.fileSystem.Directory.Exists(this.project.PathOf("/Content/Textures").FullPath).Should().BeFalse("a refused import creates no folder");

        _ = this.WriteOutside("recovered.tga", [4, 5, 6]);
        var texture = await this.import.CreateAsync(request, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = texture.Should().Be(AssetPath.Parse("/Content/Textures/Recovered.otex"));
        _ = this.fileSystem.File.Exists(this.project.PathOf("/Content/Textures/Recovered.otex.json").FullPath).Should().BeTrue();
    }

    [TestMethod]
    [DataRow("sky.gif", "/Content/Sky", "Sky", "albedo", 0, TextureImportCodes.ImageUnsupported)]
    [DataRow("sky.png", "/Content/Sky", "Sky/Bad", "albedo", 0, TextureImportCodes.NameInvalid)]
    [DataRow("sky.png", "/Content/Sky", "Sky", "diffuse", 0, TextureImportCodes.SettingsUnsupported)]
    [DataRow("sky.png", "/Content/Sky", "Sky", "hdr_env", 300, TextureImportCodes.FaceSizeInvalid)]
    [DataRow("sky.png", "/Engine/Sky", "Sky", "albedo", 0, TextureImportCodes.DestinationNotAuthoring)]
    [DataRow("sky.png", "/Content/SourceMedia/Sky", "Sky", "albedo", 0, TextureImportCodes.DestinationDerived)]
    public async Task ARefusedRequestSaysWhyAndWritesNothing(string imageName, string destination, string name, string intent, int panoramaFaceSize, string expected)
    {
        var image = this.WriteOutside(imageName, [1]);
        var request = this.Request(image, destination, name) with
        {
            Intent = intent,
            Cube = panoramaFaceSize > 0 ? new TextureCubeImport(CubeLayout.Panorama, panoramaFaceSize) : null,
        };

        var refusal = await RefusalOf(() => this.import.CreateAsync(request, this.TestContext.CancellationToken)).ConfigureAwait(false);

        _ = refusal.Should().Be(expected);
        _ = this.fileSystem.Directory.GetFiles(this.project.Root.FullPath, "*", SearchOption.AllDirectories).Should().BeEmpty();
    }

    [TestMethod]
    [DataRow("""{ "source": "Sky.hdr", "output": { "format": "rgba16f" }, "cube": { "equirect_to_cube": true, "cube_face_size": 512 } }""", true, true)]
    [DataRow("""{ "source": "Sky.hdr", "output": { "format": "rgba32f" }, "cube": { "cubemap": true, "cube_layout": "hcross" } }""", true, true)]
    [DataRow("""{ "source": "Sky.png", "output": { "format": "rgba8_srgb" }, "cube": { "cubemap": true, "cube_layout": "hstrip" } }""", true, false)]
    [DataRow("""{ "source": "Sky.hdr", "output": { "format": "rgba16f" } }""", false, false)]
    [DataRow("""{ "output": { "format": "rgba16f" }, "cube": { "cubemap": true } }""", false, false)]
    public void ATextureSaysWhetherItIsACubeAndKeepsRadiance(string json, bool cube, bool storesRadiance)
    {
        var descriptor = this.project.WriteFile("/Content/Sky.otex.json", json);

        // A descriptor without a source image is no texture, so it is no cube either.
        var radiance = Texture.TryRead(this.fileSystem, descriptor) is { IsCube: true } texture ? texture.StoresRadiance : (bool?)null;

        _ = radiance.Should().Be(cube ? storesRadiance : null);
    }

    [TestMethod]
    [DataRow(new[] { "Grass.png", "Rock.png" }, true)]
    [DataRow(new[] { "Grass.png" }, false)]
    [DataRow(new[] { "Rock.png" }, false)]
    public void ATextureHasItsSourceImageOnlyWhenEveryImageExists(string[] existing, bool expected)
    {
        var descriptor = this.project.WriteFile("/Content/Textures/Terrain.otex.json", """{ "source": "Grass.png", "sources": [ { "file": "../Shared/Rock.png", "layer": 1 } ] }""");
        if (existing.Contains("Grass.png"))
        {
            _ = this.project.WriteFile("/Content/Textures/Grass.png", [1]);
        }

        if (existing.Contains("Rock.png"))
        {
            _ = this.project.WriteFile("/Content/Shared/Rock.png", [2]);
        }

        _ = Texture.TryRead(this.fileSystem, descriptor)!.HasSourceImage(this.fileSystem, descriptor).Should().Be(expected);
    }

    [TestMethod]
    public async Task ASixFaceImportCopiesEveryFaceAndLetsTheCookerAssembleThem()
    {
        string[] names = ["posx", "negx", "posy", "negy", "posz", "negz"];
        foreach (var name in names)
        {
            _ = this.WriteOutside($"sky_{name}.hdr", Radiance(8, 8, name));
        }

        var selected = Path.Combine(Outside, "sky_negy.hdr");
        _ = this.import.FindCubeFaces(selected).Should().Equal(names.Select(name => Path.Combine(Outside, $"sky_{name}.hdr")));
        _ = TextureImport.StripFaceSuffix("sky_negy").Should().Be("sky");
        var request = this.Request(selected, "/Content/Sky", "Sky") with
        {
            Intent = "hdr_env",
            ColorSpace = "linear",
            Format = "rgba16f",
            Cube = new TextureCubeImport(CubeLayout.SixFaces),
        };

        _ = await this.import.CreateAsync(request, this.TestContext.CancellationToken).ConfigureAwait(false);

        var suffixes = TextureImport.FaceSuffixes;
        for (var face = 0; face < suffixes.Count; ++face)
        {
            var copied = await this.fileSystem.File.ReadAllTextAsync(this.project.PathOf($"/Content/Sky/Sky{suffixes[face]}.hdr").FullPath, this.TestContext.CancellationToken).ConfigureAwait(true);
            _ = copied.Should().EndWith(names[face]);
        }

        using var descriptor = this.ReadJson("/Content/Sky/Sky.otex.json");
        _ = descriptor.RootElement.GetProperty("source").GetString().Should().Be("Sky_px.hdr");
        _ = descriptor.RootElement.GetProperty("cube").GetProperty("cubemap").GetBoolean().Should().BeTrue();
        _ = descriptor.RootElement.GetProperty("cube").TryGetProperty("cube_layout", out _).Should().BeFalse();
    }

    [TestMethod]
    public void SixFacesOfDifferentSizesAreRefused()
    {
        foreach (var name in new[] { "px", "nx", "py", "ny", "pz", "nz" })
        {
            _ = this.WriteOutside($"sky_{name}.hdr", Radiance(8, string.Equals(name, "pz", StringComparison.Ordinal) ? 16 : 8, name));
        }

        var request = this.Request(Path.Combine(Outside, "sky_px.hdr"), "/Content/Sky", "Sky") with { Cube = new TextureCubeImport(CubeLayout.SixFaces) };

        var refusal = RefusalOf(() => this.import.ResolveTarget(request));

        _ = refusal.Should().Be(TextureImportCodes.CubeFacesMismatched);
    }

    [TestMethod]
    public void SixFaceImportNeedsAllSixFaces()
    {
        _ = this.WriteOutside("sky_px.hdr", Radiance(8, 8, "px"));
        var request = this.Request(Path.Combine(Outside, "sky_px.hdr"), "/Content/Sky", "Sky") with { Cube = new TextureCubeImport(CubeLayout.SixFaces) };

        var refusal = RefusalOf(() => this.import.ResolveTarget(request));

        _ = refusal.Should().Be(TextureImportCodes.CubeFacesIncomplete);
    }

    [TestMethod]
    public void ACrossLayoutRefusesAnImageThatIsNotExactlyItsFaceGrid()
    {
        var image = this.WriteOutside("diagram.hdr", Radiance(599, 451, string.Empty));
        var request = this.Request(image, "/Content/Sky", "Diagram") with { Cube = new TextureCubeImport(CubeLayout.HorizontalCross) };

        var refusal = RefusalOf(() => this.import.ResolveTarget(request));

        _ = refusal.Should().Be(TextureImportCodes.LayoutMismatch);
    }

    [TestMethod]
    [DataRow(1024, 512, CubeLayout.Panorama)]
    [DataRow(1536, 256, CubeLayout.HorizontalStrip)]
    [DataRow(256, 1536, CubeLayout.VerticalStrip)]
    [DataRow(1024, 768, CubeLayout.HorizontalCross)]
    [DataRow(768, 1024, CubeLayout.VerticalCross)]
    public void ACubeLayoutIsDetectedFromTheImageSize(int width, int height, CubeLayout expected)
    {
        var image = this.WriteOutside("sky.hdr", Radiance(width, height, string.Empty));

        var size = this.import.ReadDimensions(image);

        _ = size.Should().Be((width, height));
        _ = TextureImport.DetectCubeLayout(width, height).Should().Be(expected);
    }

    // A Radiance HDR header: the resolution line follows the blank line that ends the text header.
    private static string Radiance(int width, int height, string body)
        => string.Create(CultureInfo.InvariantCulture, $"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n{body}");

    private static async Task<string?> RefusalOf(Func<Task> action)
    {
        try
        {
            await action().ConfigureAwait(false);
            return null;
        }
        catch (TextureImportException refusal)
        {
            return refusal.Code;
        }
    }

    private static string? RefusalOf(Func<TextureImportTarget> action)
    {
        try
        {
            _ = action();
            return null;
        }
        catch (TextureImportException refusal)
        {
            return refusal.Code;
        }
    }

    private TextureImportRequest Request(string image, string destination, string name)
        => new(this.project.Context, image, AssetPath.Parse(destination), name, "albedo", "srgb", "rgba8_srgb");

    private string WriteOutside(string name, byte[] content)
    {
        var path = Path.Combine(Outside, name);
        this.fileSystem.File.WriteAllBytes(path, content);
        return path;
    }

    private string WriteOutside(string name, string content) => this.WriteOutside(name, Encoding.ASCII.GetBytes(content));

    private JsonDocument ReadJson(string path)
        => JsonDocument.Parse(this.fileSystem.File.ReadAllBytes(this.project.PathOf(path).FullPath));
}
