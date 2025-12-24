// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Assets.Import.Geometry;
using Oxygen.Assets.Import.Gltf;
using Oxygen.Assets.Model;
using Oxygen.Assets.Persistence.LooseCooked.V1;
using Oxygen.Assets.Resolvers;

namespace Oxygen.Assets.Tests;

[TestClass]
public sealed class FileSystemAssetResolverTests
{
    private string tempDir = null!;
    private string projectRoot = null!;
    private string contentRoot = null!;
    private string importedRoot = null!;

    public TestContext TestContext { get; set; }

    [TestInitialize]
    public void Setup()
    {
        this.tempDir = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString());
        this.projectRoot = Path.Combine(this.tempDir, "MyProject");
        this.contentRoot = Path.Combine(this.projectRoot, "Content");
        this.importedRoot = Path.Combine(this.projectRoot, ".imported", "Content");
        _ = Directory.CreateDirectory(this.contentRoot);
        _ = Directory.CreateDirectory(this.importedRoot);
    }

    [TestCleanup]
    public void Cleanup()
    {
        if (Directory.Exists(this.tempDir))
        {
            Directory.Delete(this.tempDir, recursive: true);
        }
    }

    [TestMethod]
    public async Task ResolveAsync_WithValidGeometry_ShouldReturnGeometryAsset()
    {
        // Arrange
        var resolver = new FileSystemAssetResolver("Content", this.contentRoot, this.importedRoot);
        const string modelName = "MyBox";

        // Simulate an imported artifact existing in .imported/Content/Models/MyBox__mesh__0000.ogeo
        var relativePath = $"Models/{modelName}__mesh__0000.ogeo";
        var importedPath = Path.Combine(this.importedRoot, relativePath);
        _ = Directory.CreateDirectory(Path.GetDirectoryName(importedPath)!);

        var importedGeometry = new ImportedGeometry(
            "MyBox",
            [],
            [],
            [
                new ImportedSubMesh(
                    "SubMesh1",
                    new AssetKey(0, 0),
                    0,
                    0,
                    0,
                    0,
                    new ImportedBounds(Vector3.Zero, Vector3.Zero)),
            ],
            new ImportedBounds(Vector3.Zero, Vector3.Zero));

        var stream = File.Create(importedPath);
        await using (stream.ConfigureAwait(true))
        {
            await ImportedGeometrySerializer.WriteAsync(stream, importedGeometry, this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var uri = new Uri($"asset://Content/{relativePath}");

        // Act
        var asset = await resolver.ResolveAsync(uri).ConfigureAwait(false);

        // Assert
        _ = asset.Should().NotBeNull();
        _ = asset.Should().BeOfType<GeometryAsset>();
        var geo = (GeometryAsset)asset!;
        _ = geo.Uri.Should().Be(uri);
        _ = geo.Source.Should().NotBeNull();
        _ = geo.Source!.SubMeshes.Should().ContainSingle();
        _ = geo.Lods.Should().ContainSingle();
        _ = geo.Lods[0].SubMeshes.Should().ContainSingle();
    }

    [TestMethod]
    public async Task ResolveAsync_WithValidMaterialSource_ShouldReturnMaterialAsset()
    {
        // Arrange
        var resolver = new FileSystemAssetResolver("Content", this.contentRoot, this.importedRoot);
        const string materialName = "Wood";
        var relativePath = $"Materials/{materialName}.omat";
        var sourcePath = Path.Combine(this.contentRoot, "Materials", $"{materialName}.omat.json");

        _ = Directory.CreateDirectory(Path.GetDirectoryName(sourcePath)!);

        const string json = """
            {
              "Schema": "oxygen.material.v1",
              "Type": "PBR",
              "Name": "Wood Material",
              "PbrMetallicRoughness": {
                "BaseColorFactor": [1.0, 0.0, 0.0, 1.0],
                "MetallicFactor": 0.5,
                "RoughnessFactor": 0.1
              },
              "AlphaMode": "OPAQUE",
              "DoubleSided": false
            }
            """;

        await File.WriteAllTextAsync(sourcePath, json, this.TestContext.CancellationToken).ConfigureAwait(false);

        // Act
        var uri = new Uri($"asset://Content/{relativePath}");
        var asset = await resolver.ResolveAsync(uri).ConfigureAwait(false);

        // Assert
        _ = asset.Should().NotBeNull();
        _ = asset.Should().BeOfType<MaterialAsset>();
        var mat = (MaterialAsset)asset!;
        _ = mat.Uri.Should().Be(uri);
        _ = mat.Source.Should().NotBeNull();
        _ = mat.Source!.Name.Should().Be("Wood Material");
        _ = mat.Source.PbrMetallicRoughness.BaseColorR.Should().Be(1.0f);
    }

    [TestMethod]
    public async Task ResolveAsync_WithMissingFile_ShouldReturnNull()
    {
        // Arrange
        var resolver = new FileSystemAssetResolver("Content", this.contentRoot, this.importedRoot);
        var uri = new Uri("asset://Content/Missing.omat");

        // Act
        var asset = await resolver.ResolveAsync(uri).ConfigureAwait(false);

        // Assert
        _ = asset.Should().BeNull();
    }

    [TestMethod]
    public async Task ResolveAsync_WithInvalidAuthority_ShouldReturnNull()
    {
        // Arrange
        var resolver = new FileSystemAssetResolver("Content", this.contentRoot, this.importedRoot);
        var uri = new Uri("asset://Engine/BasicShapes/Cube.ogeo");

        // Act
        var asset = await resolver.ResolveAsync(uri).ConfigureAwait(false);

        // Assert
        _ = asset.Should().BeNull();
    }
}
