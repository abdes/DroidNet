// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

[TestClass]
public sealed class TextureSourceAssetImporterTests
{
    [TestMethod]
    public async Task CreateAsyncCopiesImageAndWritesNamedDescriptorWithReviewedSettings()
    {
        var root = Path.Combine(Path.GetTempPath(), "OxygenTextureImport", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            var image = Path.Combine(root, "Outside", "wood.PNG");
            Directory.CreateDirectory(Path.GetDirectoryName(image)!);
            await File.WriteAllBytesAsync(image, [1, 2, 3]).ConfigureAwait(false);
            var request = new TextureSourceImportRequest(
                CreateProject(root), image, new Uri("asset:///Content/Textures"), "Wood",
                "albedo", "srgb", "rgba8_srgb");

            var assetUri = await TextureSourceAssetImporter.CreateAsync(request).ConfigureAwait(false);
            var expectedDirectory = Path.Combine(root, "Content", "Textures");
            var descriptorPath = Path.Combine(expectedDirectory, "Wood.otex.json");
            using var descriptor = JsonDocument.Parse(await File.ReadAllBytesAsync(descriptorPath).ConfigureAwait(false));

            _ = assetUri.Should().Be(new Uri("asset:///Content/Textures/Wood.otex.json"));
            _ = File.ReadAllBytes(Path.Combine(expectedDirectory, "Wood.png")).Should().Equal([1, 2, 3]);
            _ = descriptor.RootElement.GetProperty("source").GetString().Should().Be("Wood.png");
            _ = descriptor.RootElement.GetProperty("virtual_path").GetString().Should().Be("/Content/Textures/Wood.otex");
            _ = descriptor.RootElement.GetProperty("intent").GetString().Should().Be("albedo");
            _ = descriptor.RootElement.GetProperty("decode").GetProperty("color_space").GetString().Should().Be("srgb");
            _ = descriptor.RootElement.GetProperty("output").GetProperty("format").GetString().Should().Be("rgba8_srgb");
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [TestMethod]
    public async Task CreateAsyncRejectsNameCollisionWithoutReplacingExistingAssets()
    {
        var root = Path.Combine(Path.GetTempPath(), "OxygenTextureImport", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            var image = Path.Combine(root, "wood.tga");
            await File.WriteAllBytesAsync(image, [9, 8, 7]).ConfigureAwait(false);
            var folder = Path.Combine(root, "Content", "Textures");
            Directory.CreateDirectory(folder);
            var descriptor = Path.Combine(folder, "Wood.otex.json");
            await File.WriteAllTextAsync(descriptor, "owned").ConfigureAwait(false);
            var request = new TextureSourceImportRequest(
                CreateProject(root), image, new Uri("asset:///Content/Textures"), "Wood",
                "albedo", "srgb", "rgba8_srgb");

            Func<Task> import = () => TextureSourceAssetImporter.CreateAsync(request);
            _ = await import.Should().ThrowAsync<IOException>().ConfigureAwait(false);
            var existing = await File.ReadAllTextAsync(descriptor).ConfigureAwait(false);
            _ = existing.Should().Be("owned");
            _ = Directory.GetFiles(folder).Should().ContainSingle().Which.Should().Be(descriptor);
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [TestMethod]
    public async Task MissingImageSourceCanBeRecoveredBeforeImport()
    {
        var root = Path.Combine(Path.GetTempPath(), "OxygenTextureImport", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            var image = Path.Combine(root, "Outside", "recovered.tga");
            var request = new TextureSourceImportRequest(
                CreateProject(root), image, new Uri("asset:///Content/Textures"), "Recovered",
                "normal", "linear", "bc7");

            Func<Task> initialImport = () => TextureSourceAssetImporter.CreateAsync(request);
            _ = await initialImport.Should().ThrowAsync<ArgumentException>().ConfigureAwait(false);
            _ = Directory.Exists(Path.Combine(root, "Content", "Textures")).Should().BeFalse();

            Directory.CreateDirectory(Path.GetDirectoryName(image)!);
            await File.WriteAllBytesAsync(image, [4, 5, 6]).ConfigureAwait(false);
            var assetUri = await TextureSourceAssetImporter.CreateAsync(request).ConfigureAwait(false);

            _ = assetUri.Should().Be(new Uri("asset:///Content/Textures/Recovered.otex.json"));
            _ = File.Exists(Path.Combine(root, "Content", "Textures", "Recovered.otex.json")).Should().BeTrue();
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [TestMethod]
    [DataRow("""{ "output": { "format": "rgba16f" }, "cube": { "equirect_to_cube": true } }""", true, true)]
    [DataRow("""{ "output": { "format": "rgba32f" }, "cube": { "cubemap": true, "cube_layout": "hcross" } }""", true, true)]
    [DataRow("""{ "output": { "format": "rgba8_srgb" }, "cube": { "cubemap": true, "cube_layout": "hstrip" } }""", true, false)]
    [DataRow("""{ "output": { "format": "rgba16f" } }""", false, false)]
    public void ReadCubeDescriptorReportsWhetherTheCubeKeepsRadiance(string json, bool cube, bool storesRadiance)
    {
        var path = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N") + ".otex.json");
        File.WriteAllText(path, json);
        try
        {
            _ = TextureSourceAssetImporter.ReadCubeDescriptor(path).Should().Be(cube ? new CubeDescriptorInfo(storesRadiance) : null);
            _ = TextureSourceAssetImporter.IsCubeDescriptor(path).Should().Be(cube);
        }
        finally
        {
            File.Delete(path);
        }
    }

    [TestMethod]
    public async Task SixFaceImportCopiesEveryFaceAndLetsTheCookerAssembleThem()
    {
        var root = Path.Combine(Path.GetTempPath(), "OxygenTextureImport", Guid.NewGuid().ToString("N"));
        var outside = Path.Combine(root, "Outside");
        Directory.CreateDirectory(outside);
        try
        {
            string[] names = ["posx", "negx", "posy", "negy", "posz", "negz"];
            foreach (var name in names)
            {
                await File.WriteAllTextAsync(Path.Combine(outside, $"sky_{name}.hdr"), $"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 8 +X 8\n{name}").ConfigureAwait(false);
            }

            var selected = Path.Combine(outside, "sky_negy.hdr");
            _ = TextureSourceAssetImporter.FindCubeFaces(selected).Should().Equal(names.Select(name => Path.Combine(outside, $"sky_{name}.hdr")));
            _ = TextureSourceAssetImporter.StripFaceSuffix("sky_negy").Should().Be("sky");
            var request = new TextureSourceImportRequest(
                CreateProject(root), selected, new Uri("asset:///Content/Sky"), "Sky", "hdr_env", "linear", "rgba16f")
            {
                Cube = new TextureCubeImport(CubeLayout.SixFaces),
            };

            _ = await TextureSourceAssetImporter.CreateAsync(request).ConfigureAwait(false);
            var folder = Path.Combine(root, "Content", "Sky");
            var suffixes = TextureSourceAssetImporter.FaceSuffixes;
            for (var face = 0; face < suffixes.Count; ++face)
            {
                _ = (await File.ReadAllTextAsync(Path.Combine(folder, $"Sky{suffixes[face]}.hdr")).ConfigureAwait(false)).Should().EndWith(names[face]);
            }

            using var descriptor = JsonDocument.Parse(await File.ReadAllBytesAsync(Path.Combine(folder, "Sky.otex.json")).ConfigureAwait(false));
            _ = descriptor.RootElement.GetProperty("source").GetString().Should().Be("Sky_px.hdr");
            _ = descriptor.RootElement.GetProperty("cube").GetProperty("cubemap").GetBoolean().Should().BeTrue();
            _ = descriptor.RootElement.GetProperty("cube").TryGetProperty("cube_layout", out _).Should().BeFalse();

            await File.WriteAllTextAsync(Path.Combine(outside, "sky_posz.hdr"), "#?RADIANCE\n\n-Y 8 +X 16\n").ConfigureAwait(false);
            var mismatched = () => TextureSourceImportTarget.Resolve(request with { Name = "Other" });
            _ = mismatched.Should().Throw<ArgumentException>().WithMessage("Cube faces must be square images of the same size.*");
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    [TestMethod]
    public void CrossLayoutRejectsAnImageThatIsNotExactlyItsFaceGrid()
    {
        var root = Path.Combine(Path.GetTempPath(), "OxygenTextureImport", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            var image = Path.Combine(root, "diagram.hdr");
            File.WriteAllText(image, "#?RADIANCE\n\n-Y 451 +X 599\n");
            var request = new TextureSourceImportRequest(
                CreateProject(root), image, new Uri("asset:///Content/Sky"), "Diagram", "hdr_env", "linear", "rgba16f")
            {
                Cube = new TextureCubeImport(CubeLayout.HorizontalCross),
            };

            var resolve = () => TextureSourceImportTarget.Resolve(request);
            _ = resolve.Should().Throw<ArgumentException>().WithMessage("A horizontal cross must be exactly 4 square faces wide and 3 high*599 × 451*");
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    private static ProjectContext CreateProject(string root) => new()
    {
        ProjectId = Guid.NewGuid(),
        ProjectRoot = root,
        Name = "Texture test",
        Category = Category.Games,
        AuthoringMounts = [new("Content", "Content")],
        LocalFolderMounts = [],
        Scenes = [],
    };
}
