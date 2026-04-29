// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
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
        await WriteMaterialAsync(source, CreateMaterial("Wood", metallicFactor: 0.0f, roughnessFactor: 0.7f)).ConfigureAwait(false);

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
        _ = document.Assets.Should().ContainSingle(asset => string.Equals(asset.VirtualPath, "/Content/Materials/Wood.omat", StringComparison.Ordinal));
        var asset = document.Assets.Single(asset => string.Equals(asset.VirtualPath, "/Content/Materials/Wood.omat", StringComparison.Ordinal));
        var cookedPath = Path.Combine(workspace.Root, ".cooked", "Content", "Materials", "Wood.omat");
        _ = asset.DescriptorSize.Should().Be((ulong)new FileInfo(cookedPath).Length);
    }

    [TestMethod]
    public async Task CookMaterialAsync_WhenRecooked_ShouldKeepSingleVirtualPathMappingAndRefreshDescriptor()
    {
        using var workspace = new TempWorkspace();
        var source = Path.Combine(workspace.Root, "Content", "Materials", "Wood.omat.json");
        await WriteMaterialAsync(source, CreateMaterial("Wood", metallicFactor: 0.1f, roughnessFactor: 0.7f)).ConfigureAwait(false);

        var service = CreateService();
        var request = new MaterialCookRequest(
            new Uri("asset:///Content/Materials/Wood.omat.json"),
            workspace.Root,
            "Content",
            "Content/Materials/Wood.omat.json");

        var first = await service.CookMaterialAsync(request, CancellationToken.None).ConfigureAwait(false);
        await WriteMaterialAsync(source, CreateMaterial("Wood", metallicFactor: 0.8f, roughnessFactor: 0.2f)).ConfigureAwait(false);
        var second = await service.CookMaterialAsync(request, CancellationToken.None).ConfigureAwait(false);

        _ = first.State.Should().Be(MaterialCookState.Cooked);
        _ = second.State.Should().Be(MaterialCookState.Cooked);

        var indexPath = Path.Combine(workspace.Root, ".cooked", "Content", "container.index.bin");
        using var index = File.OpenRead(indexPath);
        var document = LooseCookedIndex.Read(index);
        _ = document.Assets
            .Where(static asset => string.Equals(asset.VirtualPath, "/Content/Materials/Wood.omat", StringComparison.Ordinal))
            .Should()
            .ContainSingle();

        var cookedBytes = await File.ReadAllBytesAsync(Path.Combine(workspace.Root, ".cooked", "Content", "Materials", "Wood.omat")).ConfigureAwait(false);
        _ = ReadUnorm16(cookedBytes, 0x7C).Should().BeApproximately(0.8f, 0.0001f);
        _ = ReadUnorm16(cookedBytes, 0x7E).Should().BeApproximately(0.2f, 0.0001f);
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
        await WriteMaterialAsync(source, CreateMaterial("Gold", metallicFactor: 1.0f, roughnessFactor: 0.25f)).ConfigureAwait(false);

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

    private static async Task WriteMaterialAsync(string path, MaterialSource material)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var stream = File.Create(path);
        await using (stream.ConfigureAwait(false))
        {
            MaterialSourceWriter.Write(stream, material);
        }
    }

    private static MaterialSource CreateMaterial(string name, float metallicFactor, float roughnessFactor)
        => new(
            schema: "oxygen.material.v1",
            type: "PBR",
            name: name,
            pbrMetallicRoughness: new MaterialPbrMetallicRoughness(
                baseColorR: 0.4f,
                baseColorG: 0.25f,
                baseColorB: 0.1f,
                baseColorA: 1.0f,
                metallicFactor: metallicFactor,
                roughnessFactor: roughnessFactor,
                baseColorTexture: null,
                metallicRoughnessTexture: null),
            normalTexture: null,
            occlusionTexture: null,
            alphaMode: MaterialAlphaMode.Opaque,
            alphaCutoff: 0.5f,
            doubleSided: false);

    private static float ReadUnorm16(byte[] bytes, int offset)
        => BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(offset, 2)) / 65535.0f;

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
