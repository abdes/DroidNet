// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Cooking.Unit.Tests;

/// <summary>What the Cooking panel does around a cook: saving its inputs, going to an issue, inspecting and showing outputs.</summary>
[TestClass]
public sealed class CookRecoveryTests
{
    private const string SceneName = "Main";
    private const string ProjectFolder = @"C:\CookingTests";

    private readonly Mock<IAssetCatalog> catalog = new();
    private readonly Mock<IDocumentService> documents = new();
    private readonly Mock<IUnsavedCookInputs> unsaved = new();
    private readonly Mock<IPlacementService> placement = new();
    private readonly SceneSelectionService selection = new();
    private readonly Mock<IAssetReveal> reveal = new();
    private readonly Mock<ICookedInspections> inspections = new();

    private TestSession? session;

    /// <summary>Closes the test session of the project the test activated.</summary>
    [TestCleanup]
    public void CloseSession() => this.session?.Dispose();

    /// <summary>Saving asks the document service before a cook and returns its answer.</summary>
    /// <param name="saved">Whether the document service lets the cook run.</param>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public async Task SaveInputsAsksTheDocumentServiceBeforeTheCookAsync(bool saved)
    {
        var request = new CookRunRequest(CookTargetKind.Project, ScopeUri: null);
        _ = this.documents
            .Setup(service => service.SaveBeforeAsync(SaveBeforeOperation.Cook, It.IsAny<Func<CancellationToken, Task<IReadOnlyList<UnsavedDocument>>>>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(saved);

        var result = await this.CreateRecovery().SaveInputsAsync(request).ConfigureAwait(false);

        _ = result.Should().Be(saved);
        this.documents.Verify(
            service => service.SaveBeforeAsync(SaveBeforeOperation.Cook, It.IsAny<Func<CancellationToken, Task<IReadOnlyList<UnsavedDocument>>>>(), It.IsAny<CancellationToken>()),
            Times.Once);
    }

    /// <summary>A project run inspects everything, with no asset.</summary>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    public async Task InspectOfAProjectRunAsksForNoScopeAndNoAssetAsync()
    {
        var project = this.ActivateProject();

        var result = await this.CreateRecovery().InspectAsync(RunOf(project, new(CookTargetKind.Project, ScopeUri: null))).ConfigureAwait(false);

        _ = result.Should().BeTrue();
        this.inspections.Verify(
            service => service.InspectAsync(It.Is<CookedInspectionRequest>(request => request.Scope == null && request.Asset == null && !request.Validate)),
            Times.Once);
    }

    /// <summary>An asset or current scene run inspects its scope and its asset.</summary>
    /// <param name="kind">The run's target.</param>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    [DataRow(CookTargetKind.Asset)]
    [DataRow(CookTargetKind.CurrentScene)]
    public async Task InspectOfAnAssetRunAsksForItsScopeAndAssetAsync(CookTargetKind kind)
    {
        var project = this.ActivateProject();
        var scope = AssetPath.Parse("/Content/Scenes/Main.oscene");

        var result = await this.CreateRecovery().InspectAsync(RunOf(project, new(kind, scope))).ConfigureAwait(false);

        _ = result.Should().BeTrue();
        this.inspections.Verify(
            service => service.InspectAsync(It.Is<CookedInspectionRequest>(request => request.Scope == scope && request.Asset == scope && !request.Validate)),
            Times.Once);
    }

    /// <summary>A folder run inspects its scope without an asset.</summary>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    public async Task InspectOfAFolderRunAsksForItsScopeButNoAssetAsync()
    {
        var project = this.ActivateProject();
        var scope = AssetPath.Parse("/Content/Scenes");

        var result = await this.CreateRecovery().InspectAsync(RunOf(project, new(CookTargetKind.Folder, scope))).ConfigureAwait(false);

        _ = result.Should().BeTrue();
        this.inspections.Verify(
            service => service.InspectAsync(It.Is<CookedInspectionRequest>(request => request.Scope == scope && request.Asset == null && !request.Validate)),
            Times.Once);
    }

    /// <summary>A run of another project inspects nothing.</summary>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    public async Task InspectOfAnotherProjectsRunAsksNothingAsync()
    {
        var project = this.ActivateProject();
        var other = project with { ProjectId = Guid.NewGuid() };

        var result = await this.CreateRecovery().InspectAsync(RunOf(other, new(CookTargetKind.Project, ScopeUri: null))).ConfigureAwait(false);

        _ = result.Should().BeFalse();
        this.inspections.VerifyNoOtherCalls();
    }

    /// <summary>The imported outputs of the run are revealed.</summary>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    public async Task ShowImportedAssetsRevealsTheRunsOutputsAsync()
    {
        var project = this.ActivateProject();
        var run = ImportedRunOf(project);
        _ = run.ImportedOutputs.Should().NotBeEmpty();
        _ = this.reveal.Setup(service => service.RevealAsync(It.IsAny<IReadOnlyList<AssetPath>>())).ReturnsAsync(value: true);

        var result = await this.CreateRecovery().ShowImportedAssetsAsync(run).ConfigureAwait(false);

        _ = result.Should().BeTrue();
        this.reveal.Verify(service => service.RevealAsync(It.Is<IReadOnlyList<AssetPath>>(assets => assets.SequenceEqual(run.ImportedOutputs))), Times.Once);
    }

    /// <summary>A run without imported outputs reveals nothing.</summary>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    public async Task ShowImportedAssetsOfARunWithoutOutputsRevealsNothingAsync()
    {
        var project = this.ActivateProject();

        var result = await this.CreateRecovery().ShowImportedAssetsAsync(RunOf(project, new(CookTargetKind.Project, ScopeUri: null))).ConfigureAwait(false);

        _ = result.Should().BeFalse();
        this.reveal.VerifyNoOtherCalls();
    }

    /// <summary>A run of another project reveals nothing.</summary>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    public async Task ShowImportedAssetsOfAnotherProjectsRunRevealsNothingAsync()
    {
        var project = this.ActivateProject();

        var result = await this.CreateRecovery().ShowImportedAssetsAsync(ImportedRunOf(project with { ProjectId = Guid.NewGuid() })).ConfigureAwait(false);

        _ = result.Should().BeFalse();
        this.reveal.VerifyNoOtherCalls();
    }

    /// <summary>An issue of another project's run is not navigated to.</summary>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    public async Task GoToPropertyOfAnotherProjectsRunIsRefusedAsync()
    {
        var project = this.ActivateProject();
        var issue = Issue(SceneId.New(), "somewhere", SceneStartingPropertyPaths.StartingCamera);

        var result = await this.CreateRecovery().GoToPropertyAsync(RunOf(project with { ProjectId = Guid.NewGuid() }, new(CookTargetKind.Project, ScopeUri: null)), issue).ConfigureAwait(false);

        _ = result.Should().BeFalse();
        this.placement.VerifyNoOtherCalls();
    }

    /// <summary>
    /// A scene property issue opens its scene, selects the scene and asks its property to be focused, with the
    /// Properties and Cooking panels shown.
    /// </summary>
    /// <param name="property">The property path the issue names.</param>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    [DataRow(SceneStartingPropertyPaths.StartingCamera)]
    [DataRow(SceneEnvironmentConstraints.AerialStartPropertyPath)]
    public async Task GoToScenePropertyFocusesThePropertyOfItsSceneAsync(string property)
    {
        using var scenes = new TestSceneDocuments(TestProject.Create(new MockFileSystem(), ProjectFolder));
        var project = this.ActivateProject(scenes);
        var id = SceneId.New();
        var document = await OpenSceneAsync(scenes, id).ConfigureAwait(false);
        var focused = new List<ScenePropertyFocusEventArgs>();
        this.selection.PropertyFocusRequested += (_, args) => focused.Add(args);
        this.ArrangeScene(scenes, document);
        var issue = Issue(id, scenes.FileOf(SceneName).Path.FullPath, property);

        var result = await this.CreateRecovery().GoToPropertyAsync(RunOf(project, new(CookTargetKind.Project, ScopeUri: null)), issue).ConfigureAwait(false);

        _ = result.Should().BeTrue();
        _ = this.selection.GetContext(document.Id).Kind.Should().Be(SceneSelectionKind.Scene);
        var request = focused.Should().ContainSingle().Subject;
        _ = request.DocumentId.Should().Be(document.Id);
        _ = request.PropertyPath.Should().Be(property);
        this.placement.Verify(service => service.OpenDocumentAsync(scenes.FileOf(SceneName).Asset, It.IsAny<WindowPlacement>(), It.IsAny<CancellationToken>()), Times.Once);
        this.placement.Verify(service => service.ShowPanelsAsync(It.Is<IReadOnlyList<string>>(panels => panels.SequenceEqual(new[] { "props", "cook" })), It.IsAny<WindowPlacement>()), Times.Once);
    }

    /// <summary>A script slot issue selects its node and asks for no property focus.</summary>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    public async Task GoToScriptSlotSelectsItsNodeWithoutPropertyFocusAsync()
    {
        using var scenes = new TestSceneDocuments(TestProject.Create(new MockFileSystem(), ProjectFolder));
        var project = this.ActivateProject(scenes);
        var id = SceneId.New();
        var document = await OpenSceneAsync(scenes, id).ConfigureAwait(false);
        var node = SceneContent.Of(document).Scene.RootNodes.Single();
        var focused = new List<ScenePropertyFocusEventArgs>();
        this.selection.PropertyFocusRequested += (_, args) => focused.Add(args);
        this.ArrangeScene(scenes, document);
        var property = "Scripting/" + Guid.NewGuid().ToString("D");
        var issue = Issue(id, scenes.FileOf(SceneName).Path.FullPath, property, node.Id);

        var result = await this.CreateRecovery().GoToPropertyAsync(RunOf(project, new(CookTargetKind.Project, ScopeUri: null)), issue).ConfigureAwait(false);

        _ = result.Should().BeTrue();
        _ = this.selection.GetContext(document.Id).SelectedNodeIds.Should().ContainSingle().Which.Should().Be(node.Id);
        _ = focused.Should().BeEmpty();
        this.placement.Verify(service => service.ShowPanelsAsync(It.Is<IReadOnlyList<string>>(panels => panels.SequenceEqual(new[] { "props", "cook" })), It.IsAny<WindowPlacement>()), Times.Once);
    }

    private static async Task<Document> OpenSceneAsync(TestSceneDocuments scenes, SceneId id)
    {
        var built = new Scene { Name = SceneName };
        built.RootNodes.Add(new SceneNode(built) { Name = "Cube" });
        return await scenes.OpenAsync(built.Dehydrate() with { Id = id }).ConfigureAwait(false);
    }

    private static CookRunSnapshot RunOf(ProjectContext project, CookRunRequest request)
        => new()
        {
            OperationId = Guid.NewGuid(),
            ProjectId = project.ProjectId,
            ProjectRoot = project.ProjectRoot,
            DisplayName = "Cook",
            Request = request,
        };

    private static CookRunSnapshot ImportedRunOf(ProjectContext project)
    {
        var source = AssetPath.Parse("/Content/Models/Ship.glb");
        var product = AssetPath.Parse("/Content/Models/Ship.glb/Paint.omat");
        return RunOf(project, new(CookTargetKind.Asset, source)) with
        {
            State = CookRunState.Succeeded,
            CompletedAt = DateTimeOffset.UtcNow,
            Assets = ImmutableDictionary<AssetPath, CookRunAsset>.Empty
                .Add(source, new(source, ContentCookAssetKind.ForeignSource, CookAssetState.Reused))
                .Add(product, new(product, ContentCookAssetKind.Material, CookAssetState.Updated)),
        };
    }

    private static DiagnosticRecord Issue(SceneId scene, string path, string property, NodeId? node = null)
    {
        var payload = new Dictionary<string, string>(StringComparer.Ordinal) { ["PropertyPath"] = property };
        if (node is { } nodeId)
        {
            payload["NodeId"] = nodeId.Value.ToString("D");
        }

        return new()
        {
            OperationId = Guid.NewGuid(),
            Domain = FailureDomain.ContentPipeline,
            Severity = DiagnosticSeverity.Error,
            Code = "UI.Cook.InvalidInput",
            Message = "The property is invalid.",
            AffectedPath = path,
            AffectedEntity = new AffectedScope { SceneId = scene },
            SuggestedAction = new PrimaryAction { ActionId = "Cook.GoToProperty", Label = "Go to property", Kind = PrimaryActionKind.Custom, Payload = payload },
        };
    }

    private ProjectContext ActivateProject(TestSceneDocuments? scenes = null)
    {
        var context = scenes?.Project.Context ?? TestProject.Create(new MockFileSystem(), ProjectFolder).Context;
        this.session?.Dispose();
        this.session = new TestSession(context);
        return context;
    }

    private void ArrangeScene(TestSceneDocuments scenes, Document document)
    {
        var file = scenes.FileOf(SceneName);
        IReadOnlyList<AssetRecord> records = [new(file.Asset.WithAuthoringSuffix())];
        _ = this.catalog.Setup(service => service.QueryAsync(It.IsAny<AssetQuery>(), It.IsAny<CancellationToken>())).ReturnsAsync(records);
        _ = this.placement
            .Setup(service => service.OpenDocumentAsync(file.Asset, It.IsAny<WindowPlacement>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(new DocumentPlacement { Asset = file.Asset, Document = document });
        _ = this.placement
            .Setup(service => service.ShowPanelsAsync(It.IsAny<IReadOnlyList<string>>(), It.IsAny<WindowPlacement>()))
            .ReturnsAsync(value: true);
    }

    private CookRecovery CreateRecovery()
    {
        _ = this.inspections.Setup(service => service.InspectAsync(It.IsAny<CookedInspectionRequest>())).ReturnsAsync(value: true);
        this.session ??= new TestSession(TestProject.Create(new MockFileSystem(), ProjectFolder).Context);
        return new(this.session, this.catalog.Object, this.documents.Object, this.unsaved.Object, this.placement.Object, this.selection, this.reveal.Object, this.inspections.Object);
    }
}
