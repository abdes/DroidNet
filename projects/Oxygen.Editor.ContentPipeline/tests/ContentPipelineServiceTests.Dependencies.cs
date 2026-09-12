// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies saved dependency discovery, coherent capture and authored geometry cooking.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>Discovers shared scene dependencies, geometry buffers and settings without unrelated consuming scenes.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task DiscoveryIncludesTransitiveMaterialAndBufferInputsOnce()
    {
        using var workspace = new TempWorkspace();
        var geometryUri = new Uri("asset:///Content/Geometry/AuthoredCube.ogeo.json");
        var materialUri = new Uri("asset:///Content/Materials/Red.omat.json");
        WriteAuthoredGeometry(workspace, withBuffer: true);
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        workspace.WriteText("Content/Geometry/AuthoredCube.ogeo.json.import.json", "{\"Version\":1}");
        AddGeometryNode(workspace, geometryUri, "First");
        AddGeometryNode(workspace, geometryUri, "Second");
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var sceneUri = new Uri("asset:///Content/Scenes/Main.oscene.json");
        var discovery = new CookDependencyDiscovery(workspace.Documents);

        var graph = await discovery.DiscoverAsync(workspace.ProjectContext, [CookInputResolver.Resolve(workspace.ProjectContext, sceneUri, ContentCookInputRole.Primary)], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = graph.Assets.Should().HaveCount(3);
        _ = graph.Diagnostics.Should().BeEmpty();
        _ = graph.Dependencies[sceneUri].Should().Equal(geometryUri);
        _ = graph.Dependencies[geometryUri].Should().Equal(materialUri);
        _ = graph.Files.Should().ContainSingle(input => input.RelativePath == "Content/Geometry/mesh.bin" && !input.IsAbsent);
        _ = graph.Files.Should().ContainSingle(input => input.RelativePath == "Content/Geometry/AuthoredCube.ogeo.json.import.json" && !input.IsAbsent);
        _ = graph.Files.Should().ContainSingle(input => input.RelativePath == "Content/Materials/Red.omat.json.import.json" && input.IsAbsent);
        _ = graph.PublishedReferences.Should().BeEmpty();
        _ = graph.Builtins.Should().BeEmpty();

        var capture = new CookInputSnapshotCapture(workspace.Documents, workspace.CookCoordinator);
        var result = await workspace.CookCoordinator.RunAsync(
            (operation, token) => capture.CaptureAsync(operation, _ => Task.FromResult<IReadOnlyList<CookSnapshotInput>>(graph.Files), "qualified-fixture-artifacts", token),
            this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Snapshot.Should().NotBeNull();
        workspace.WriteText("Content/Geometry/mesh.bin", "changed buffer bytes");
        var capturedBuffer = await File.ReadAllTextAsync(Path.Combine(result.Snapshot!.InputRoot, "Content", "Geometry", "mesh.bin"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = capturedBuffer.Should().Be("saved buffer bytes");
        _ = File.Exists(Path.Combine(result.Snapshot.InputRoot, "Content", "Materials", "Red.omat.json.import.json")).Should().BeFalse();

        var materialOnly = await discovery.DiscoverAsync(workspace.ProjectContext, [CookInputResolver.Resolve(workspace.ProjectContext, materialUri, ContentCookInputRole.Primary)], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = materialOnly.Assets.Should().ContainSingle().Which.AssetUri.Should().Be(materialUri);
        _ = materialOnly.Files.Should().NotContain(input => input.RelativePath.EndsWith(".oscene.json", StringComparison.Ordinal));
    }

    /// <summary>Settings created after discovery force rediscovery instead of silently disappearing from the input set.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task DiscoveryAndCaptureRetryWhenOptionalSettingsAppear()
    {
        using var workspace = new TempWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var input = CookInputResolver.Resolve(workspace.ProjectContext, new("asset:///Content/Materials/Red.omat.json"), ContentCookInputRole.Primary);
        var discovery = new CookDependencyDiscovery(workspace.Documents);
        var capture = new CookInputSnapshotCapture(workspace.Documents, workspace.CookCoordinator);
        var attempts = 0;
        var result = await workspace.CookCoordinator.RunAsync(
            (operation, token) => capture.CaptureAsync(
            operation,
            async cancellationToken =>
            {
                var graph = await discovery.DiscoverAsync(workspace.ProjectContext, [input], cancellationToken).ConfigureAwait(false);
                if (++attempts == 1)
                {
                    workspace.WriteText("Content/Materials/Red.omat.json.import.json", "{\"Version\":1}");
                }

                return graph.Files;
            },
            "qualified-fixture-artifacts",
            token),
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = attempts.Should().Be(2);
        _ = result.Snapshot.Should().NotBeNull();
        var snapshot = result.Snapshot!;
        var settings = snapshot.Inputs.Single(file => file.RelativePath.EndsWith(".import.json", StringComparison.Ordinal));
        _ = settings.IsAbsent.Should().BeFalse();
        var savedSettings = await File.ReadAllTextAsync(Path.Combine(snapshot.InputRoot, settings.RelativePath), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = savedSettings.Should().Be("{\"Version\":1}");
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Changed later");
        var savedMaterial = snapshot.Inputs.Single(file => file.AssetUri == input.AssetUri);
        var savedBytes = await File.ReadAllBytesAsync(Path.Combine(snapshot.InputRoot, savedMaterial.RelativePath), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = Convert.ToHexString(SHA256.HashData(savedBytes)).Should().Be(savedMaterial.DiscoveryHash);
        _ = Encoding.UTF8.GetString(savedBytes).Should().Contain("\"Red\"").And.NotContain("Changed later");
    }

    /// <summary>A material shared with a dirty scene can be captured independently; a scene's dirty material blocks its capture.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task DirtyDependencyBlocksDiscoveryButDirtyConsumerDoesNot()
    {
        using var workspace = new TempWorkspace();
        WriteAuthoredGeometry(workspace, withBuffer: false);
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var material = CookInputResolver.Resolve(workspace.ProjectContext, new("asset:///Content/Materials/Red.omat.json"), ContentCookInputRole.Primary);
        AddGeometryNode(workspace, new("asset:///Content/Geometry/AuthoredCube.ogeo.json"), "Uses red");
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var scene = CookInputResolver.Resolve(workspace.ProjectContext, new("asset:///Content/Scenes/Main.oscene.json"), ContentCookInputRole.Primary);
        var dirtyScene = SavedState(scene.SourceAbsolutePath) with { IsDirty = true, Revision = 2 };
        using var sceneRegistration = workspace.Documents.Register(scene.SourceAbsolutePath, _ => Task.FromResult<CookDocumentReadLease?>(new(dirtyScene, static () => { })));
        var discovery = new CookDependencyDiscovery(workspace.Documents);
        var graph = await discovery.DiscoverAsync(workspace.ProjectContext, [material], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = graph.Assets.Should().ContainSingle();
        var dirtyMaterial = SavedState(material.SourceAbsolutePath) with { IsDirty = true, Revision = 2 };
        using var materialRegistration = workspace.Documents.Register(material.SourceAbsolutePath, _ => Task.FromResult<CookDocumentReadLease?>(new(dirtyMaterial, static () => { })));
        var geometry = CookInputResolver.Resolve(workspace.ProjectContext, new("asset:///Content/Geometry/AuthoredCube.ogeo.json"), ContentCookInputRole.Primary);
        Func<Task> discover = () => discovery.DiscoverAsync(workspace.ProjectContext, [geometry], this.TestContext.CancellationToken);
        var failure = await discover.Should().ThrowAsync<Cooking.CookInputsNeedSaveException>().ConfigureAwait(false);
        _ = failure.Which.Documents.Should().ContainSingle().Which.DocumentId.Should().Be(dirtyMaterial.DocumentId);
    }

    /// <summary>Cooked references remain distinct from source inputs and cannot be claimed fresh by discovery.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task DiscoverySeparatesPublishedReferencesAndEngineRecipes()
    {
        using var workspace = new TempWorkspace();
        var published = new Uri("asset:///Content/Geometry/Published.ogeo");
        var builtin = new Uri("asset:///Engine/Generated/BasicShapes/Cylinder");
        AddGeometryNode(workspace, published, "Published");
        AddGeometryNode(workspace, builtin, "Builtin");
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var scene = CookInputResolver.Resolve(workspace.ProjectContext, new("asset:///Content/Scenes/Main.oscene.json"), ContentCookInputRole.Primary);

        var graph = await new CookDependencyDiscovery(workspace.Documents).DiscoverAsync(workspace.ProjectContext, [scene], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = graph.Assets.Should().ContainSingle();
        _ = graph.PublishedReferences.Should().Equal(published);
        _ = graph.Builtins.Should().Equal(builtin);
    }

    /// <summary>Cooking an authored geometry discovers its saved material before the first native cook.</summary>
    /// <param name="sceneCook">Whether to cook a saved scene referencing the native geometry identity.</param>
    /// <returns>The asynchronous native test operation.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow(false)]
    [DataRow(true)]
    public async Task AuthoredGeometryCookIncludesUncookedMaterialDependency(bool sceneCook)
    {
        using var workspace = new TempWorkspace();
        WriteAuthoredGeometry(workspace, withBuffer: false);
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api);

        if (sceneCook)
        {
            AddGeometryNode(workspace, new("asset:///Content/Geometry/AuthoredCube.ogeo"), "Native identity");
            workspace.Scene.SetEnvironment(new World.Serialization.SceneEnvironmentData
            {
                ExposureMode = World.Serialization.ExposureMode.Auto,
                PostProcess = new World.Serialization.PostProcessEnvironmentData { ExposureMode = World.Serialization.ExposureMode.Auto },
            });
            await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        }

        var result = sceneCook
            ? await service.CookCurrentSceneAsync(new("asset:///Content/Scenes/Main.oscene.json"), this.TestContext.CancellationToken).ConfigureAwait(false)
            : await service.CookAssetAsync(new("asset:///Content/Geometry/AuthoredCube.ogeo.json"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Succeeded, string.Join(Environment.NewLine, result.Diagnostics.Select(static diagnostic => diagnostic.TechnicalMessage ?? diagnostic.Message)));
        _ = result.CookedAssets.Should().HaveCount(sceneCook ? 3 : 2);
        _ = result.CookedAssets.Should().Contain(asset => asset.SourceAssetUri == new Uri("asset:///Content/Materials/Red.omat.json"));
        _ = result.CookedAssets.Should().Contain(asset => asset.SourceAssetUri == new Uri("asset:///Content/Geometry/AuthoredCube.ogeo.json"));
    }

    /// <summary>Source traversal cannot capture files outside the retained project.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task GeometryMediaOutsideProjectFailsDiscovery()
    {
        using var workspace = new TempWorkspace();
        WriteAuthoredGeometry(workspace, withBuffer: true);
        const string source = "Content/Geometry/AuthoredCube.ogeo.json";
        var descriptor = JsonNode.Parse(workspace.ReadText(source))!;
        descriptor["buffers"]![0]!["uri"] = "../../../outside.bin";
        workspace.WriteText(source, descriptor.ToJsonString());
        var geometry = CookInputResolver.Resolve(workspace.ProjectContext, new("asset:///" + source), ContentCookInputRole.Primary);
        var graph = await new CookDependencyDiscovery(workspace.Documents).DiscoverAsync(workspace.ProjectContext, [geometry], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = graph.Diagnostics.Should().ContainSingle(diagnostic => diagnostic.Message.Contains("outside the project", StringComparison.Ordinal) && diagnostic.AffectedVirtualPath == geometry.AssetUri.AbsolutePath);
    }

    /// <summary>Absolute paths to retained media are captured without altering authored descriptor bytes.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task GeometryAbsoluteRetainedMediaIsDiscovered()
    {
        using var workspace = new TempWorkspace();
        WriteAuthoredGeometry(workspace, withBuffer: true);
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        const string source = "Content/Geometry/AuthoredCube.ogeo.json";
        var descriptor = JsonNode.Parse(workspace.ReadText(source))!;
        var bufferPath = Path.Combine(workspace.Root, "Content", "Geometry", "mesh.bin");
        descriptor["buffers"]![0]!["uri"] = bufferPath;
        workspace.WriteText(source, descriptor.ToJsonString());
        var geometry = CookInputResolver.Resolve(workspace.ProjectContext, new("asset:///" + source), ContentCookInputRole.Primary);

        var graph = await new CookDependencyDiscovery(workspace.Documents).DiscoverAsync(workspace.ProjectContext, [geometry], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = graph.Files.Should().ContainSingle(file => file.SourcePath == bufferPath);
        _ = workspace.ReadText(source).Should().Be(descriptor.ToJsonString());
    }

    /// <summary>The scalar editor cook must not silently discard a saved texture reference.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task TextureBearingMaterialFailsBeforeNativeImport()
    {
        using var workspace = new TempWorkspace();
        const string source = "Content/Materials/Red.omat.json";
        workspace.WriteMaterial(source, "Red");
        var material = JsonNode.Parse(workspace.ReadText(source))!;
        material["PbrMetallicRoughness"]!["BaseColorTexture"] = JsonNode.Parse("""{"Source":"asset:///Content/Textures/Red.png"}""");
        workspace.WriteText(source, material.ToJsonString());
        var api = CreateSuccessfulApi(workspace);
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, []), api);

        var result = await service.CookAssetAsync(new("asset:///" + source), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic => diagnostic.Message.Contains("scalar", StringComparison.Ordinal));
        _ = workspace.CookCoordinator.Runs.Single().Assets.Values.Should().ContainSingle(asset => asset.AssetUri == new Uri("asset:///" + source) && asset.State == Cooking.CookAssetState.Failed);
        _ = api.ImportedManifest.Should().BeNull();
        _ = workspace.ReadText(source).Should().Be(material.ToJsonString());
    }

    /// <summary>Independent input errors are collected and scoped before native work can change output.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task FolderDiscoveryReportsIndependentAssetErrorsTogether()
    {
        using var workspace = new TempWorkspace();
        workspace.WriteText("Content/Materials/First.omat.json", "invalid first");
        workspace.WriteText("Content/Materials/Second.omat.json", "invalid second");
        var api = CreateSuccessfulApi(workspace);
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, []), api);

        var result = await service.CookFolderAsync(new("asset:///Content/Materials"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().HaveCount(2);
        _ = result.Diagnostics.Select(static diagnostic => diagnostic.AffectedVirtualPath).Should().BeEquivalentTo("/Content/Materials/First.omat.json", "/Content/Materials/Second.omat.json");
        _ = workspace.CookCoordinator.Runs.Single().Assets.Values.Should().OnlyContain(static asset => asset.State == Cooking.CookAssetState.Failed);
        _ = api.ImportedManifest.Should().BeNull();
    }

    private static void AddGeometryNode(TempWorkspace workspace, Uri uri, string name)
    {
        var node = new SceneNode(workspace.Scene) { Name = name };
        _ = node.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = new AssetReference<GeometryAsset>(uri) });
        workspace.Scene.RootNodes.Add(node);
    }

    private static void WriteAuthoredGeometry(TempWorkspace workspace, bool withBuffer)
    {
        var catalog = JsonNode.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Fixtures", "BuiltinGeometryCatalog.json")))!;
        var descriptor = catalog["geometries"]![0]!["descriptor"]!;
        descriptor["name"] = "AuthoredCube";
        descriptor["lods"]![0]!["submeshes"]![0]!["material_ref"] = "/Content/Materials/Red.omat";
        if (withBuffer)
        {
            descriptor["buffers"] = JsonNode.Parse("""[{"uri":"mesh.bin","virtual_path":"/Content/Buffers/Mesh.obuf"}]""");
            workspace.WriteText("Content/Geometry/mesh.bin", "saved buffer bytes");
        }

        workspace.WriteText("Content/Geometry/AuthoredCube.ogeo.json", descriptor.ToJsonString());
    }
}
