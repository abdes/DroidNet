// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V3;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Cooking;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class MaterialCookTests
{
    public TestContext TestContext { get; set; } = null!;
    /// <summary>Verifies the workflow can import Build And Verify Loose Cooked Output.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookMaterialAsync_ShouldImportBuildAndVerifyLooseCookedOutput()
    {
        using var workspace = new MaterialWorkspace();
        var source = Path.Combine(workspace.Root, "Content", "Materials", "Wood.omat.json");
        await WriteMaterialAsync(source, CreateMaterial("Wood", metallicFactor: 0.0f, roughnessFactor: 0.7f)).ConfigureAwait(false);

        var service = CreateService(workspace);
        var result = await service.CookMaterialAsync(
            new MaterialCookRequest(
                new Uri("asset:///Content/Materials/Wood.omat.json"),
                workspace.Root,
                "Content",
                "Content/Materials/Wood.omat.json"),
            CancellationToken.None).ConfigureAwait(false);

        _ = result.State.Should().Be(MaterialCookState.Cooked);
        _ = result.Cook.Should().NotBeNull();
        _ = result.Cook!.InputSnapshot.Should().NotBeNull();
        _ = result.Cook.InputsAreCurrent.Should().BeTrue();
        _ = workspace.CookCoordinator.Runs.Should().ContainSingle();
        _ = result.OperationId.Should().Be(workspace.CookCoordinator.Runs.Single().OperationId);
        _ = result.CookedMaterialUri.Should().Be(new Uri("asset:///Content/Materials/Wood.omat"));
        _ = File.Exists(Path.Combine(workspace.CookedRoot, "Materials", "Wood.omat")).Should().BeTrue();

        var indexPath = Path.Combine(workspace.CookedRoot, "container.index.bin");
        _ = File.Exists(indexPath).Should().BeTrue();
        var index = File.OpenRead(indexPath);
        await using var indexLifetime = index.ConfigureAwait(false);
        var document = LooseCookedIndex.Read(index);
        _ = document.Assets.Should().ContainSingle(asset => string.Equals(asset.VirtualPath, "/Content/Materials/Wood.omat", StringComparison.Ordinal));
        var asset = document.Assets.Single(asset => string.Equals(asset.VirtualPath, "/Content/Materials/Wood.omat", StringComparison.Ordinal));
        var cookedPath = Path.Combine(workspace.CookedRoot, "Materials", "Wood.omat");
        _ = asset.DescriptorSize.Should().Be((ulong)new FileInfo(cookedPath).Length);
    }

    /// <summary>Verifies the workflow can keep Single Virtual Path Mapping And Refresh Descriptor.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookMaterialAsync_WhenRecooked_ShouldKeepSingleVirtualPathMappingAndRefreshDescriptor()
    {
        using var workspace = new MaterialWorkspace();
        var source = Path.Combine(workspace.Root, "Content", "Materials", "Wood.omat.json");
        await WriteMaterialAsync(source, CreateMaterial("Wood", metallicFactor: 0.1f, roughnessFactor: 0.7f)).ConfigureAwait(false);

        var service = CreateService(workspace);
        var request = new MaterialCookRequest(
            new Uri("asset:///Content/Materials/Wood.omat.json"),
            workspace.Root,
            "Content",
            "Content/Materials/Wood.omat.json");

        var first = await service.CookMaterialAsync(request, CancellationToken.None).ConfigureAwait(false);
        var descriptorPath = Path.Combine(workspace.CookedRoot, "Materials", "Wood.omat");
        var originalBytes = await File.ReadAllBytesAsync(descriptorPath, CancellationToken.None).ConfigureAwait(false);
        await WriteMaterialAsync(source, CreateMaterial("Wood", metallicFactor: 0.8f, roughnessFactor: 0.2f)).ConfigureAwait(false);
        var second = await service.CookMaterialAsync(request, CancellationToken.None).ConfigureAwait(false);

        _ = first.State.Should().Be(MaterialCookState.Cooked);
        _ = second.State.Should().Be(MaterialCookState.Cooked);

        var indexPath = Path.Combine(workspace.CookedRoot, "container.index.bin");
        var index = File.OpenRead(indexPath);
        await using var indexLifetime = index.ConfigureAwait(false);
        var document = LooseCookedIndex.Read(index);
        _ = document.Assets
            .Where(static asset => string.Equals(asset.VirtualPath, "/Content/Materials/Wood.omat", StringComparison.Ordinal))
            .Should()
            .ContainSingle();

        var cookedBytes = await File.ReadAllBytesAsync(Path.Combine(workspace.CookedRoot, "Materials", "Wood.omat"), CancellationToken.None).ConfigureAwait(false);
        _ = cookedBytes.Should().NotEqual(originalBytes);
        _ = document.Assets.Single().DescriptorSha256.Span.ToArray().Should().Equal(System.Security.Cryptography.SHA256.HashData(cookedBytes));
    }

    /// <summary>Verifies the workflow can write Under Mount Cooked Root.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CookMaterialAsync_WhenMountNameDiffersFromFolder_ShouldWriteUnderMountCookedRoot()
    {
        using var workspace = new MaterialWorkspace("Authoring");
        var source = Path.Combine(workspace.Root, "Authoring", "Materials", "Gold.omat.json");
        await WriteMaterialAsync(source, CreateMaterial("Gold", metallicFactor: 1.0f, roughnessFactor: 0.25f)).ConfigureAwait(false);

        var service = CreateService(workspace);
        var result = await service.CookMaterialAsync(
            new MaterialCookRequest(
                new Uri("asset:///Content/Materials/Gold.omat.json"),
                workspace.Root,
                "Content",
                "Authoring/Materials/Gold.omat.json"),
            CancellationToken.None).ConfigureAwait(false);

        _ = result.State.Should().Be(MaterialCookState.Cooked);
        _ = result.CookedMaterialUri.Should().Be(new Uri("asset:///Content/Materials/Gold.omat"));
        _ = File.Exists(Path.Combine(workspace.CookedRoot, "Materials", "Gold.omat")).Should().BeTrue();
        _ = workspace.CookedRoot.Should().Contain(Path.Combine(".cooked", "generations"));
    }

    private static MaterialCookService CreateService(MaterialWorkspace workspace)
        => new(workspace.NativePipeline.Pipeline, workspace.ContextService, NullLogger<MaterialCookService>.Instance);

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
    private sealed partial class MaterialWorkspace : IDisposable
    {
        private Oxygen.Testing.NativeContentPipelineFixture? nativePipeline;
        public MaterialWorkspace(string authoringFolder = "Content")
        {
            this.Root = Path.Combine(Path.GetTempPath(), "oxygen-content-pipeline-tests", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(this.Root);
            var project = new ProjectInfo("TestProject", Category.Games, this.Root)
            {
                AuthoringMounts = [new ProjectMountPoint("Content", authoringFolder)],
            };
            File.WriteAllText(Path.Combine(this.Root, "Project.oxy"), ProjectInfo.ToJson(project));
            this.ContextService.Activate(ProjectContext.FromProjectInfo(project));
            this.CookCoordinator = new ContentCookCoordinator(this.ContextService, NullLogger<ContentCookCoordinator>.Instance);
        }

        public string Root { get; }

        public string CookedRoot
        {
            get
            {
                var head = System.Text.Json.JsonSerializer.Deserialize<global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationHead>(File.ReadAllBytes(global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationPaths.Head(this.Root)), global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationDocument.JsonOptions)!;
                var document = System.Text.Json.JsonSerializer.Deserialize<global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationDocument>(File.ReadAllBytes(global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationPaths.Document(this.Root, head.PublicationId)), global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationDocument.JsonOptions)!;
                return document.Roots.Single(static root => root.Owner == global::Oxygen.Editor.ContentPipeline.Publication.CookPublicationRootOwner.Project && root.Name == "Content").ResolvePath(this.Root);
            }
        }

        public ProjectContextService ContextService { get; } = new();

        public ContentCookCoordinator CookCoordinator { get; }
        public global::Oxygen.Editor.ContentPipeline.Snapshots.CookDocumentRegistry Documents { get; } = new();

        public Oxygen.Testing.NativeContentPipelineFixture NativePipeline => this.nativePipeline ??= new(this.ContextService, this.CookCoordinator, this.Documents);

        public void Dispose()
        {
            this.ContextService.Close();
            this.CookCoordinator.Dispose();
            this.nativePipeline?.Dispose();
            if (Directory.Exists(this.Root))
            {
                Directory.Delete(this.Root, recursive: true);
            }
        }
    }
}
