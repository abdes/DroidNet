// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Assets.Import;
using Oxygen.Assets.Import.Materials;
using Oxygen.Assets.Model;
using Oxygen.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Editor.ContentPipeline.Tests;

[TestClass]
public sealed class MaterialCookServiceTests
{
    [TestMethod]
    public async Task CookMaterialAsync_ShouldImportBuildAndVerifyLooseCookedOutput()
    {
        using var workspace = new TempWorkspace();
        var source = Path.Combine(workspace.Root, "Content", "Materials", "Wood.omat.json");
        Directory.CreateDirectory(Path.GetDirectoryName(source)!);
        await using (var stream = File.Create(source))
        {
            MaterialSourceWriter.Write(
                stream,
                new MaterialSource(
                    schema: "oxygen.material.v1",
                    type: "PBR",
                    name: "Wood",
                    pbrMetallicRoughness: new MaterialPbrMetallicRoughness(
                        baseColorR: 0.4f,
                        baseColorG: 0.25f,
                        baseColorB: 0.1f,
                        baseColorA: 1.0f,
                        metallicFactor: 0.0f,
                        roughnessFactor: 0.7f,
                        baseColorTexture: null,
                        metallicRoughnessTexture: null),
                    normalTexture: null,
                    occlusionTexture: null,
                    alphaMode: MaterialAlphaMode.Opaque,
                    alphaCutoff: 0.5f,
                    doubleSided: false));
        }

        var service = CreateService();
        var result = await service.CookMaterialAsync(
            new MaterialCookRequest(
                new Uri("asset:///Content/Materials/Wood.omat.json"),
                workspace.Root,
                "Content",
                "Content/Materials/Wood.omat.json"),
            CancellationToken.None).ConfigureAwait(false);

        _ = result.State.Should().Be(MaterialCookState.Cooked);
        _ = result.CookedMaterialUri.Should().Be(new Uri("asset:///Content/Materials/Wood.omat"));
        _ = File.Exists(Path.Combine(workspace.Root, ".cooked", "Content", "Materials", "Wood.omat")).Should().BeTrue();

        var indexPath = Path.Combine(workspace.Root, ".cooked", "Content", "container.index.bin");
        _ = File.Exists(indexPath).Should().BeTrue();
        using var index = File.OpenRead(indexPath);
        var document = LooseCookedIndex.Read(index);
        _ = document.Assets.Should().Contain(asset => asset.VirtualPath == "/Content/Materials/Wood.omat");
    }

    [TestMethod]
    public async Task CookMaterialAsync_WhenRequestMissingProjectFacts_ShouldReject()
    {
        var service = CreateService();

        var result = await service.CookMaterialAsync(
            new MaterialCookRequest(
                new Uri("asset:///Content/Materials/Wood.omat.json"),
                ProjectRoot: string.Empty,
                MountName: "Content",
                SourceRelativePath: "Content/Materials/Wood.omat.json"),
            CancellationToken.None).ConfigureAwait(false);

        _ = result.State.Should().Be(MaterialCookState.Rejected);
    }

    [TestMethod]
    public async Task CookMaterialAsync_WhenMountNameDiffersFromFolder_ShouldWriteUnderMountCookedRoot()
    {
        using var workspace = new TempWorkspace();
        var source = Path.Combine(workspace.Root, "Authoring", "Materials", "Gold.omat.json");
        Directory.CreateDirectory(Path.GetDirectoryName(source)!);
        await using (var stream = File.Create(source))
        {
            MaterialSourceWriter.Write(
                stream,
                new MaterialSource(
                    schema: "oxygen.material.v1",
                    type: "PBR",
                    name: "Gold",
                    pbrMetallicRoughness: new MaterialPbrMetallicRoughness(
                        baseColorR: 1.0f,
                        baseColorG: 0.75f,
                        baseColorB: 0.15f,
                        baseColorA: 1.0f,
                        metallicFactor: 1.0f,
                        roughnessFactor: 0.25f,
                        baseColorTexture: null,
                        metallicRoughnessTexture: null),
                    normalTexture: null,
                    occlusionTexture: null,
                    alphaMode: MaterialAlphaMode.Opaque,
                    alphaCutoff: 0.5f,
                    doubleSided: false));
        }

        var service = CreateService();
        var result = await service.CookMaterialAsync(
            new MaterialCookRequest(
                new Uri("asset:///Content/Materials/Gold.omat.json"),
                workspace.Root,
                "Content",
                "Authoring/Materials/Gold.omat.json"),
            CancellationToken.None).ConfigureAwait(false);

        _ = result.State.Should().Be(MaterialCookState.Cooked);
        _ = result.CookedMaterialUri.Should().Be(new Uri("asset:///Content/Materials/Gold.omat"));
        _ = File.Exists(Path.Combine(workspace.Root, ".cooked", "Content", "Materials", "Gold.omat")).Should().BeTrue();
        _ = File.Exists(Path.Combine(workspace.Root, ".cooked", "Authoring", "Materials", "Gold.omat")).Should().BeFalse();
    }

    private static MaterialCookService CreateService()
    {
        var registry = new ImporterRegistry();
        registry.Register(new MaterialSourceImporter());
        var importService = new ImportService(registry);
        return new MaterialCookService(importService, NullLogger<MaterialCookService>.Instance);
    }

    private sealed class TempWorkspace : IDisposable
    {
        public TempWorkspace()
        {
            this.Root = Path.Combine(Path.GetTempPath(), "oxygen-content-pipeline-tests", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(this.Root);
        }

        public string Root { get; }

        public void Dispose()
        {
            if (Directory.Exists(this.Root))
            {
                Directory.Delete(this.Root, recursive: true);
            }
        }
    }
}
