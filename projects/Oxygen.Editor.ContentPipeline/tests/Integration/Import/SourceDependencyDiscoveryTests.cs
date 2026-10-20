// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.DependencyScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.SavedSourceScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Import;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class SourceDependencyDiscoveryTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Discovers shared scene dependencies, geometry buffers and settings without unrelated consuming scenes.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task DiscoveryIncludesTransitiveMaterialAndBufferInputsOnce()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        var geometryAsset = AssetPath.Parse("/Content/Geometry/AuthoredCube.ogeo");
        var geometrySource = AssetPath.Parse("/Content/Geometry/AuthoredCube.ogeo");
        var materialSource = AssetPath.Parse("/Content/Materials/Red.omat");
        WriteAuthoredGeometry(workspace, withBuffer: true);
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        workspace.WriteText("Content/Geometry/AuthoredCube.ogeo.json.import.json", "{\"Version\":1}");
        AddGeometryNode(workspace, geometryAsset, "First");
        AddGeometryNode(workspace, geometryAsset, "Second");
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var sceneUri = AssetPath.Parse("/Content/Scenes/Main.oscene");
        var discovery = NativeSourceFactsFixture.CreateDiscovery(workspace);

        var graph = await discovery.DiscoverAsync(workspace.ProjectContext, [CookInputResolver.Resolve(workspace.ProjectContext, sceneUri, ContentCookInputRole.Primary)], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = graph.Assets.Should().HaveCount(3);
        _ = graph.Diagnostics.Should().BeEmpty();
        _ = graph.Dependencies[sceneUri].Should().Equal(geometrySource);
        _ = graph.Dependencies[geometrySource].Should().Equal(materialSource);
        _ = graph.Files.Should().ContainSingle(input => input.RelativePath == "Content/Geometry/mesh.bin" && !input.IsAbsent);
        _ = graph.Files.Should().ContainSingle(input => input.RelativePath == "Content/Geometry/AuthoredCube.ogeo.json.import.json" && !input.IsAbsent);
        _ = graph.Files.Should().ContainSingle(input => input.RelativePath == "Content/Materials/Red.omat.json.import.json" && input.IsAbsent);
        _ = graph.PublishedReferences.Should().BeEmpty();

        var capture = new CookInputSnapshotCapture(workspace.Documents, workspace.CookCoordinator);
        var result = await workspace.CookCoordinator.RunAsync(
            (operation, token) => capture.CaptureAsync(operation, _ => Task.FromResult<IReadOnlyList<CookSnapshotInput>>(graph.Files), "compatible-fixture-artifacts", token),
            this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Snapshot.Should().NotBeNull();
        workspace.WriteText("Content/Geometry/mesh.bin", "changed buffer bytes");
        var capturedBuffer = await File.ReadAllTextAsync(Path.Combine(result.Snapshot!.InputRoot, "Content", "Geometry", "mesh.bin"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = capturedBuffer.Should().Be("saved buffer bytes");
        _ = File.Exists(Path.Combine(result.Snapshot.InputRoot, "Content", "Materials", "Red.omat.json.import.json")).Should().BeFalse();

        var materialOnly = await discovery.DiscoverAsync(workspace.ProjectContext, [CookInputResolver.Resolve(workspace.ProjectContext, materialSource, ContentCookInputRole.Primary)], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = materialOnly.Assets.Should().ContainSingle().Which.AssetUri.Should().Be(materialSource);
        _ = materialOnly.Files.Should().NotContain(input => input.RelativePath.EndsWith(".oscene.json", StringComparison.Ordinal));
    }

    /// <summary>An action or a mapping context named under a project input source cooks with that source, which the
    /// scene that uses it then depends on.</summary>
    /// <returns>The asynchronous dependency-discovery operation.</returns>
    [TestMethod]
    public async Task DiscoveryCooksTheInputSourceOfAReferencedProduct()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        var sceneUri = AssetPath.Parse("/Content/Scenes/Main.oscene");
        var controls = AssetPath.Parse("/Content/Input/Controls.input");
        workspace.Scene.SetReferences(new SceneReferencesData { InputActions = [AssetPath.Parse("/Content/Input/Controls.input/Jump.oiact")] });
        workspace.Scene.SetStartingInputContexts([new StartingInputContextData(AssetPath.Parse("/Content/Input/Controls.input/Gameplay.oimap"))]);
        workspace.WriteText(
            "Content/Input/Controls.input.json",
            """{ "actions": [ { "name": "Jump", "type": "bool" } ], "contexts": [ { "name": "Gameplay", "priority": 3, "mappings": [] } ] }""");
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var discovery = NativeSourceFactsFixture.CreateDiscovery(workspace);

        var graph = await discovery.DiscoverAsync(
            workspace.ProjectContext,
            [CookInputResolver.Resolve(workspace.ProjectContext, sceneUri, ContentCookInputRole.Primary)],
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = graph.Diagnostics.Should().BeEmpty();
        _ = graph.Assets.Should().Contain(input => input.AssetUri == controls && input.Kind == ContentCookAssetKind.Input);
        _ = graph.Dependencies[sceneUri].Should().Equal(controls);
        _ = graph.PublishedReferences.Should().BeEmpty();
    }

    /// <summary>Typed scene references that no project source produces remain cooked identities rather than becoming
    /// source jobs.</summary>
    /// <returns>The asynchronous dependency-discovery operation.</returns>
    [TestMethod]
    public async Task DiscoveryTracksTypedSceneReferencesAsCookedOutputs()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        var sceneUri = AssetPath.Parse("/Content/Scenes/Main.oscene");
        var scriptOutputUri = AssetPath.Parse("/Content/Scripts/Orbit.oscript");
        var actionOutputUri = AssetPath.Parse("/Content/Input/Jump.oiact");
        var physicsOutputUri = AssetPath.Parse("/Content/Physics/Main.opscene");
        AssetPath[] typedReferences = [scriptOutputUri, actionOutputUri, physicsOutputUri];
        workspace.Scene.SetReferences(new SceneReferencesData
        {
            Scripts = [scriptOutputUri],
            InputActions = [actionOutputUri],
            PhysicsSidecars = [physicsOutputUri],
        });
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var discovery = NativeSourceFactsFixture.CreateDiscovery(workspace);

        var graph = await discovery.DiscoverAsync(
            workspace.ProjectContext,
            [CookInputResolver.Resolve(workspace.ProjectContext, sceneUri, ContentCookInputRole.Primary)],
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = graph.Diagnostics.Should().BeEmpty();
        _ = graph.Assets.Should().ContainSingle(input => input.AssetUri == sceneUri);
        _ = graph.Dependencies[sceneUri].Should().BeEquivalentTo(typedReferences);
        _ = graph.NativeReferences[sceneUri].Should().BeEquivalentTo(typedReferences);
        _ = graph.PublishedReferences.Should().BeEquivalentTo(typedReferences);

        using var missingOutputs = await CookedLibraryReadSet.AcquireAsync(workspace.ProjectContext, workspace.EngineContent, this.TestContext.CancellationToken).ConfigureAwait(false);
        var missingDiagnostics = missingOutputs.Apply(graph).Diagnostics
            .Where(static issue => issue.Code == "asset_cook.library_reference_missing")
            .ToArray();
        _ = missingDiagnostics.Should().HaveCount(typedReferences.Length);
        _ = missingDiagnostics.Should().Contain(issue => issue.Message.Contains(physicsOutputUri.VirtualPath, StringComparison.Ordinal));

        using var projectOutputs = await CookedLibraryReadSet.AcquireAsync(
            workspace.ProjectContext,
            workspace.EngineContent,
            this.TestContext.CancellationToken,
            knownOutputs: typedReferences).ConfigureAwait(false);
        _ = projectOutputs.Apply(graph).Diagnostics.Should().BeEmpty();

        using var sourceOnly = await CookedLibraryReadSet.AcquireAsync(
            workspace.ProjectContext,
            workspace.EngineContent,
            this.TestContext.CancellationToken,
            knownOutputs: [AssetPath.Parse("/Content/Scripts/Orbit.lua")]).ConfigureAwait(false);
        _ = sourceOnly.Apply(graph).Diagnostics
            .Should().Contain(issue => issue.Code == "asset_cook.library_reference_missing" && issue.Message.Contains(scriptOutputUri.VirtualPath, StringComparison.Ordinal));
    }

    /// <summary>Settings created after discovery force rediscovery instead of silently disappearing from the input set.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task DiscoveryAndCaptureRetryWhenOptionalSettingsAppear()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var input = CookInputResolver.Resolve(workspace.ProjectContext, AssetPath.Parse("/Content/Materials/Red.omat"), ContentCookInputRole.Primary);
        var discovery = NativeSourceFactsFixture.CreateDiscovery(workspace);
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
            "compatible-fixture-artifacts",
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
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        WriteAuthoredGeometry(workspace, withBuffer: false);
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var material = CookInputResolver.Resolve(workspace.ProjectContext, AssetPath.Parse("/Content/Materials/Red.omat"), ContentCookInputRole.Primary);
        AddGeometryNode(workspace, AssetPath.Parse("/Content/Geometry/AuthoredCube.ogeo"), "Uses red");
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var scene = CookInputResolver.Resolve(workspace.ProjectContext, AssetPath.Parse("/Content/Scenes/Main.oscene"), ContentCookInputRole.Primary);
        var dirtyScene = SavedState(scene.SourceAbsolutePath.FullPath) with { IsDirty = true };
        using var sceneRegistration = workspace.Documents.Register(scene.SourceAbsolutePath, _ => Task.FromResult<CookDocumentReadLease?>(new(dirtyScene, static () => { })));
        var discovery = NativeSourceFactsFixture.CreateDiscovery(workspace);
        var graph = await discovery.DiscoverAsync(workspace.ProjectContext, [material], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = graph.Assets.Should().ContainSingle();
        var dirtyMaterial = SavedState(material.SourceAbsolutePath.FullPath) with { IsDirty = true };
        using var materialRegistration = workspace.Documents.Register(material.SourceAbsolutePath, _ => Task.FromResult<CookDocumentReadLease?>(new(dirtyMaterial, static () => { })));
        var geometry = CookInputResolver.Resolve(workspace.ProjectContext, AssetPath.Parse("/Content/Geometry/AuthoredCube.ogeo"), ContentCookInputRole.Primary);
        Func<Task> discover = () => discovery.DiscoverAsync(workspace.ProjectContext, [geometry], this.TestContext.CancellationToken);
        var failure = await discover.Should().ThrowAsync<global::Oxygen.Editor.ContentPipeline.Cooking.CookInputsNeedSaveException>().ConfigureAwait(false);
        _ = failure.Which.Documents.Should().ContainSingle().Which.SourcePath.Should().Be(dirtyMaterial.SourcePath);
    }

    /// <summary>Cooked references remain distinct from source inputs and cannot be claimed fresh by discovery.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task DiscoveryTreatsEngineContentAsPublishedReference()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        var published = AssetPath.Parse("/Content/Geometry/Published.ogeo");
        var engine = EngineAssets.BasicShape("Cylinder");
        AddGeometryNode(workspace, published, "Published");
        AddGeometryNode(workspace, engine, "Engine");
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var scene = CookInputResolver.Resolve(workspace.ProjectContext, AssetPath.Parse("/Content/Scenes/Main.oscene"), ContentCookInputRole.Primary);

        var graph = await NativeSourceFactsFixture.CreateDiscovery(workspace).DiscoverAsync(workspace.ProjectContext, [scene], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = graph.Assets.Should().ContainSingle();
        _ = graph.PublishedReferences.Should().BeEquivalentTo([published, engine]);
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
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        WriteAuthoredGeometry(workspace, withBuffer: false);
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        using var cook = NativeCook.Create(workspace);
        var service = cook.Service;

        if (sceneCook)
        {
            AddGeometryNode(workspace, AssetPath.Parse("/Content/Geometry/AuthoredCube.ogeo"), "Native identity");
            workspace.Scene.SetEnvironment(new World.Serialization.SceneEnvironmentData
            {
                PostProcess = new World.Serialization.PostProcessEnvironmentData { ExposureMode = World.Serialization.ExposureMode.Auto },
            });
            await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        }

        var result = sceneCook
            ? await service.CookCurrentSceneAsync(AssetPath.Parse("/Content/Scenes/Main.oscene"), this.TestContext.CancellationToken).ConfigureAwait(false)
            : await service.CookAssetAsync(AssetPath.Parse("/Content/Geometry/AuthoredCube.ogeo"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Succeeded, string.Join(Environment.NewLine, result.Diagnostics.Select(static diagnostic => diagnostic.TechnicalMessage ?? diagnostic.Message)));
        _ = result.CookedAssets.Should().HaveCount(sceneCook ? 3 : 2);
        _ = result.CookedAssets.Should().Contain(asset => asset.SourceAssetUri == AssetPath.Parse("/Content/Materials/Red.omat"));
        _ = result.CookedAssets.Should().Contain(asset => asset.SourceAssetUri == AssetPath.Parse("/Content/Geometry/AuthoredCube.ogeo"));
    }

    /// <summary>Source traversal cannot capture files outside the retained project.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task GeometryMediaOutsideProjectFailsDiscovery()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        WriteAuthoredGeometry(workspace, withBuffer: true);
        const string source = "Content/Geometry/AuthoredCube.ogeo.json";
        var descriptor = JsonNode.Parse(workspace.ReadText(source))!;
        descriptor["buffers"]![0]!["uri"] = "../../../outside.bin";
        workspace.WriteText(source, descriptor.ToJsonString());
        var geometry = CookInputResolver.Resolve(workspace.ProjectContext, AssetPath.Parse("/" + source[..^5]), ContentCookInputRole.Primary);
        var graph = await NativeSourceFactsFixture.CreateDiscovery(workspace).DiscoverAsync(workspace.ProjectContext, [geometry], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = graph.Diagnostics.Should().ContainSingle(diagnostic => diagnostic.Message.Contains("outside the project", StringComparison.Ordinal) && diagnostic.AffectedVirtualPath == geometry.AssetUri.VirtualPath);
    }

    /// <summary>Absolute paths to retained media are captured without altering authored descriptor bytes.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task GeometryAbsoluteRetainedMediaIsDiscovered()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        WriteAuthoredGeometry(workspace, withBuffer: true);
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        const string source = "Content/Geometry/AuthoredCube.ogeo.json";
        var descriptor = JsonNode.Parse(workspace.ReadText(source))!;
        var bufferPath = Path.Combine(workspace.Root, "Content", "Geometry", "mesh.bin");
        descriptor["buffers"]![0]!["uri"] = bufferPath;
        workspace.WriteText(source, descriptor.ToJsonString());
        var geometry = CookInputResolver.Resolve(workspace.ProjectContext, AssetPath.Parse("/" + source[..^5]), ContentCookInputRole.Primary);

        var graph = await NativeSourceFactsFixture.CreateDiscovery(workspace).DiscoverAsync(workspace.ProjectContext, [geometry], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = graph.Files.Should().ContainSingle(file => file.SourcePath == FileSystemPath.From(bufferPath));
        _ = workspace.ReadText(source).Should().Be(descriptor.ToJsonString());
    }

    /// <summary>Every material channel follows the existing texture dependency and image capture path.</summary>
    /// <returns>The asynchronous dependency regression.</returns>
    [TestMethod]
    public async Task MaterialDiscoveryIncludesIndependentTexturesImagesAndLibraryReferences()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        const string source = "Content/Materials/Channels.omat.json";
        workspace.WriteText(source, """
            { "name":"Channels", "textures": {
                "base_color":{"virtual_path":"/Content/Textures/Shared.otex"},
                "normal":{"virtual_path":"/Content/Textures/Shared.otex","uv_set":1},
                "metallic":{"virtual_path":"/Content/Textures/Metal.otex"},
                "roughness":{"virtual_path":"/Content/Textures/Rough.otex"},
                "emissive":{"virtual_path":"/Library/Textures/Emit.otex"}
            } }
            """);
        foreach (var name in new[] { "Shared", "Metal", "Rough" })
        {
            workspace.WriteText($"Content/Textures/{name}.otex.json", $$"""{"source":"{{name}}.png"}""");
            workspace.WriteText($"Content/Textures/{name}.png", "saved image bytes");
        }

        var materialUri = AssetPath.Parse("/" + source[..^5]);
        var input = CookInputResolver.Resolve(workspace.ProjectContext, materialUri, ContentCookInputRole.Primary);
        var graph = await NativeSourceFactsFixture.CreateDiscovery(workspace).DiscoverAsync(workspace.ProjectContext, [input], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = graph.Diagnostics.Should().BeEmpty();
        _ = graph.Assets.Should().HaveCount(4);
        _ = graph.Dependencies[materialUri].Should().BeEquivalentTo(new[]
        {
            AssetPath.Parse("/Content/Textures/Shared.otex"),
            AssetPath.Parse("/Content/Textures/Metal.otex"),
            AssetPath.Parse("/Content/Textures/Rough.otex"),
            AssetPath.Parse("/Library/Textures/Emit.otex"),
        });
        _ = graph.Files.Count(static file => file.RelativePath.EndsWith(".png", StringComparison.Ordinal)).Should().Be(3);
        _ = graph.PublishedReferences.Should().ContainSingle().Which.Should().Be(AssetPath.Parse("/Library/Textures/Emit.otex"));
        var capture = new CookInputSnapshotCapture(workspace.Documents, workspace.CookCoordinator);
        var captured = await workspace.CookCoordinator.RunAsync(
            (operation, token) => capture.CaptureAsync(operation, _ => Task.FromResult<IReadOnlyList<CookSnapshotInput>>(graph.Files), "compatible-fixture-artifacts", token),
            this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = captured.Snapshot.Should().NotBeNull();
        workspace.WriteText("Content/Textures/Metal.png", "new image bytes");
        var saved = await File.ReadAllTextAsync(Path.Combine(captured.Snapshot!.InputRoot, "Content/Textures/Metal.png"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = saved.Should().Be("saved image bytes");
    }

    /// <summary>A missing texture descriptor blocks native work without changing the authored material.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task MissingMaterialTextureFailsBeforeNativeImport()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        const string source = "Content/Materials/Red.omat.json";
        workspace.WriteMaterial(source, "Red");
        var material = JsonNode.Parse(workspace.ReadText(source))!;
        material["textures"] = JsonNode.Parse("""{"base_color":{"virtual_path":"/Content/Textures/Red.otex"}}""");
        workspace.WriteText(source, material.ToJsonString());
        var api = CreateSuccessfulApi(workspace, NativeSourceFactsFixture.AnalyzeAsync);
        var service = CreateService(workspace, api, new CapturingSceneDescriptorGenerator([]));

        var result = await service.CookAssetAsync(AssetPath.Parse("/" + source[..^5]), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        var diagnostic = result.Diagnostics.Should().ContainSingle(diagnostic =>
            diagnostic.Code == "material.texture_reference_invalid"
            && diagnostic.AffectedVirtualPath == "/Content/Materials/Red.omat"
            && diagnostic.AffectedPath == Path.Combine(workspace.Root, "Content", "Textures", "Red.otex.json")).Which;
        _ = diagnostic.Message.Should().Contain("texture channel 'base_color' texture descriptor is missing");
        _ = api.ImportedManifest.Should().BeNull();
        _ = workspace.ReadText(source).Should().Be(material.ToJsonString());
    }

    /// <summary>A texture that adds images through <c>sources</c> needs each of them, not only its primary image.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task MissingImageOfAMultiSourceTextureFailsBeforeNativeImport()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        const string source = "Content/Materials/Red.omat.json";
        workspace.WriteMaterial(source, "Red");
        var material = JsonNode.Parse(workspace.ReadText(source))!;
        material["textures"] = JsonNode.Parse("""{"base_color":{"virtual_path":"/Content/Textures/Red.otex"}}""");
        workspace.WriteText(source, material.ToJsonString());
        workspace.WriteText("Content/Textures/Red.otex.json", """{ "virtual_path": "/Content/Textures/Red.otex", "source": "Red.png", "sources": [ { "file": "Red_1.png", "layer": 1 } ] }""");
        workspace.WriteText("Content/Textures/Red.png", "image");
        var api = CreateSuccessfulApi(workspace, NativeSourceFactsFixture.AnalyzeAsync);
        var service = CreateService(workspace, api, new CapturingSceneDescriptorGenerator([]));

        var result = await service.CookAssetAsync(AssetPath.Parse("/" + source[..^5]), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic =>
            diagnostic.Code == "material.texture_reference_invalid"
            && diagnostic.AffectedVirtualPath == "/Content/Materials/Red.omat"
            && diagnostic.AffectedPath == Path.Combine(workspace.Root, "Content", "Textures", "Red_1.png"));
        _ = api.ImportedManifest.Should().BeNull();
    }

    /// <summary>Independent input errors are collected and scoped before native work can change output.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task FolderDiscoveryReportsIndependentAssetErrorsTogether()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteText("Content/Materials/First.omat.json", "invalid first");
        workspace.WriteText("Content/Materials/Second.omat.json", "invalid second");
        var api = CreateSuccessfulApi(workspace, NativeSourceFactsFixture.AnalyzeAsync);
        var service = CreateService(workspace, api, new CapturingSceneDescriptorGenerator([]));

        var result = await service.CookFolderAsync(AssetPath.Parse("/Content/Materials"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().HaveCount(2);
        _ = result.Diagnostics.Select(static diagnostic => diagnostic.AffectedVirtualPath).Should().BeEquivalentTo("/Content/Materials/First.omat", "/Content/Materials/Second.omat");
        _ = workspace.CookCoordinator.Runs.Single().Assets.Values.Should().OnlyContain(static asset => asset.State == global::Oxygen.Editor.ContentPipeline.Cooking.CookAssetState.Failed);
        _ = api.ImportedManifest.Should().BeNull();
    }
}
