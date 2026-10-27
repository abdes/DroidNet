// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Input;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.SceneEditor.Settings;
using Oxygen.Editor.SceneEditor.Unit.Tests.TestSupport;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.SceneEditor.Unit.Tests;

/// <summary>
/// Placing instances from the scene toolbar (scene instances 6.1, 8.6): <em>Model…</em> offers imported models, one per
/// source file; <em>Scene…</em> offers authored scenes that would not make a cycle; the chosen source is placed as a
/// root named after it and selected.
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository's discovery configuration.")]
public sealed partial class SceneEditorInstanceTests
{
    private const string SceneName = "Harbor";

    private static readonly AssetPath Harbor = AssetPath.Parse($"/{TestProject.ContentMount}/{SceneName}.oscene");
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Vehicles/Car.glb");
    private static readonly AssetPath Ship = AssetPath.Parse("/Content/Vehicles/Ship.fbx");
    private static readonly AssetPath CarScene = AssetPath.Parse("/Content/Vehicles/Car.glb/Car.oscene");
    private static readonly AssetPath Watchtower = AssetPath.Parse("/Content/Watchtower.oscene");
    private static readonly AssetPath Village = AssetPath.Parse("/Content/Village.oscene");

    [TestMethod]
    public void Model_OffersImportedModelsByTheirSourceFile()
    {
        using var fixture = new Fixture();
        var requested = 0;
        fixture.Editor.InstanceSourcePickerRequested += (_, _) => requested++;

        MenuItem(fixture, "Model…").Execute(parameter: null);

        _ = requested.Should().Be(1);
        var choices = fixture.Editor.InstanceSourceChoices;
        _ = choices.Should().BeSameAs(fixture.Editor.ModelChoices);
        _ = choices!.Find(Car).Should().NotBeNull();
        _ = choices.Find(Ship).Should().BeNull("a model counts once an import names it");
        _ = choices.Find(CarScene).Should().BeNull("a model's scene product is picked as the model");
    }

    [TestMethod]
    public async Task Scene_OffersScenesThatDoNotMakeACycle()
    {
        using var fixture = new Fixture();
        fixture.References.Set(Village, Watchtower);
        fixture.References.Set(Watchtower, Harbor);

        await ((IAsyncRelayCommand)MenuItem(fixture, "Scene…")).ExecuteAsync(parameter: null).ConfigureAwait(false);

        var choices = fixture.Editor.InstanceSourceChoices;
        _ = choices.Should().BeSameAs(fixture.Editor.SceneChoices);
        _ = choices!.Find(Harbor).Should().BeNull("a scene cannot instance itself");
        _ = choices.Find(Watchtower).Should().BeNull("Watchtower instances this scene");
        _ = choices.Find(Village).Should().BeNull("Village reaches this scene through Watchtower");
        _ = choices.Find(CarScene).Should().BeNull("a model's scene product is not an authored scene");
        _ = fixture.References.Refreshes.Should().BePositive();
    }

    [TestMethod]
    public async Task Scene_OffersAnUnrelatedScene()
    {
        using var fixture = new Fixture();
        fixture.References.Set(Watchtower, Car);

        await ((IAsyncRelayCommand)MenuItem(fixture, "Scene…")).ExecuteAsync(parameter: null).ConfigureAwait(false);

        _ = fixture.Editor.InstanceSourceChoices!.Find(Watchtower).Should().NotBeNull();
    }

    [TestMethod]
    public async Task AddInstance_PlacesARootNamedAfterItsSourceAndSelectsIt()
    {
        using var fixture = new Fixture();

        var placed = await fixture.Editor.AddInstanceAsync(Car).ConfigureAwait(false);

        _ = placed.Should().BeTrue();
        var root = fixture.Scene.RootNodes.Should().ContainSingle(node => node.Components.OfType<SceneInstanceComponent>().Any()).Subject;
        _ = root.Metadata.Name.Should().Be("Car");
        _ = root.Components.OfType<SceneInstanceComponent>().Single().Source.Should().Be(Car);
        _ = fixture.Services.Selection.GetContext(fixture.Metadata.Document.Id).SelectedNodes.Should().Equal(NodeReference.To(root.Id));
    }

    [TestMethod]
    public async Task AddInstance_OfTheSceneItself_IsRefusedAndReported()
    {
        using var fixture = new Fixture();

        var placed = await fixture.Editor.AddInstanceAsync(Harbor).ConfigureAwait(false);

        _ = placed.Should().BeFalse();
        _ = fixture.Scene.RootNodes.Should().NotContain(node => node.Components.OfType<SceneInstanceComponent>().Any());
        fixture.Results.Verify(
            results => results.Publish(It.Is<OperationResult>(result => result.Diagnostics.Any(diagnostic => diagnostic.Code == SceneEditCodes.InstanceCycle))),
            Times.Once);
    }

    [TestMethod]
    public async Task AddInstance_OfASceneReachingTheScene_IsRefusedAndReported()
    {
        using var fixture = new Fixture();
        fixture.References.Set(Village, Watchtower);
        fixture.References.Set(Watchtower, Harbor);

        var placed = await fixture.Editor.AddInstanceAsync(Village).ConfigureAwait(false);

        _ = placed.Should().BeFalse();
        _ = fixture.Scene.RootNodes.Should().NotContain(node => node.Components.OfType<SceneInstanceComponent>().Any());
        fixture.Results.Verify(
            results => results.Publish(It.Is<OperationResult>(result => result.Diagnostics.Any(diagnostic => diagnostic.Code == SceneEditCodes.InstanceCycle))),
            Times.Once);
        _ = fixture.References.Refreshes.Should().BePositive("the edit checks the saved scenes as they are now");
    }

    [TestMethod]
    public async Task Views_PlaceAnInstanceInTheirEditorsDocument()
    {
        using var fixture = new Fixture();

        var elsewhere = await fixture.Services.Views.AddInstanceAsync(DroidNet.Documents.DocumentId.New(), Car).ConfigureAwait(false);
        var placed = await fixture.Services.Views.AddInstanceAsync(fixture.Metadata.Document.Id, Car).ConfigureAwait(false);

        _ = elsewhere.Should().BeFalse();
        _ = placed.Should().BeTrue();
        _ = fixture.Scene.RootNodes.Should().ContainSingle(node => node.Components.OfType<SceneInstanceComponent>().Any());
    }

    [TestMethod]
    public async Task Duplicate_OfASelectionHoldingAnInstancedNode_IsRefusedWholeAndReported()
    {
        using var fixture = new Fixture();
        _ = await fixture.Editor.AddInstanceAsync(Car).ConfigureAwait(false);
        var root = fixture.Scene.RootNodes.Single(node => node.Components.OfType<SceneInstanceComponent>().Any());
        var door = new NodeReference(root.Id, NodePath.Of(NodeId.New()));
        fixture.Services.Selection.Publish(fixture.Metadata.Document.Id, SceneSelectionContext.ForNodes([NodeReference.To(root.Id), door]), "Viewport");

        await fixture.Editor.ExecuteAsync(SceneEditorCommands.Duplicate.Id).ConfigureAwait(false);

        _ = fixture.Scene.RootNodes.Should().ContainSingle(node => node.Components.OfType<SceneInstanceComponent>().Any(), "no batch applies to part of the selection");
        fixture.Results.Verify(
            results => results.Publish(It.Is<OperationResult>(result => result.Diagnostics.Any(diagnostic => diagnostic.Code == SceneEditCodes.InstancedStructure))),
            Times.Once);
    }

    private static System.Windows.Input.ICommand MenuItem(Fixture fixture, string text)
        => fixture.Editor.QuickAddMenu.Items.Single(item => string.Equals(item.Text, text, StringComparison.Ordinal)).Command!;

    private static AssetEntry Entry(AssetPath identity, AssetKind kind, bool imported = false)
        => new(identity, identity.Name, kind, AssetState.Source, DerivedState: null, AssetRuntimeAvailability.NotApplicable, identity.VirtualPath, FileSystemPath.From("C:/Project" + identity.VirtualPath), DescriptorPath: null, CookedPath: null, AssetGuid: null, [], IsSelectable: true)
        {
            ImportSource = imported ? identity : null,
        };

    private sealed partial class Fixture : IDisposable
    {
        private readonly TestSession session;
        private readonly PreviewSettingsService previewSettings;
        private readonly BehaviorSubject<IReadOnlyList<AssetEntry>> assetItems;

        public Fixture()
        {
            this.session = new TestSession(TestProject.Create(new MockFileSystem()).Context);
            this.Metadata = TestScenes.Open(new Scene { Name = SceneName }, references: this.References);
            this.Tab = new DocumentTab(this.Metadata.Document, new DocumentEditorDescriptor(this.Metadata.Document.Kind, static _ => new object()) { IsClosable = false });
            this.assetItems = new BehaviorSubject<IReadOnlyList<AssetEntry>>(
            [
                Entry(Car, AssetKind.Model, imported: true),
                Entry(Ship, AssetKind.Model),
                Entry(CarScene, AssetKind.Scene),
                Entry(Harbor, AssetKind.Scene),
                Entry(Watchtower, AssetKind.Scene),
                Entry(Village, AssetKind.Scene),
            ]);
            var assets = new Mock<IAssetFeed>();
            _ = assets.SetupGet(feed => feed.Items).Returns(this.assetItems);
            this.previewSettings = TestSceneEditorServices.CreatePreviewSettings(this.session);
            this.Services = TestSceneEditorServices.Create(this.session, this.previewSettings, results: this.Results.Object, assets: assets.Object, references: this.References);
            this.Editor = new(this.Tab, this.Services);
            this.Editor.AttachWindow(new DocumentWindowServices(new WindowId(1), new TestDocumentArea(this.Metadata.Document), Mock.Of<ICommandService>(), Mock.Of<IPlacementService>(), Mock.Of<IAssetReveal>()));
        }

        public TestScene Metadata { get; }

        public DocumentTab Tab { get; }

        public FakeAuthoredReferences References { get; } = new();

        public Mock<IOperationResultPublisher> Results { get; } = new();

        public SceneEditorServices Services { get; }

        public SceneEditorViewModel Editor { get; }

        public Scene Scene => SceneContent.Of(this.Metadata.Document).Scene;

        public void Dispose()
        {
            this.Editor.Dispose();
            this.previewSettings.Dispose();
            this.Tab.Dispose();
            this.Metadata.Dispose();
            this.assetItems.Dispose();
            this.session.Dispose();
        }
    }
}
