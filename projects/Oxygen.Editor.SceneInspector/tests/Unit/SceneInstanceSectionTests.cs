// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Concurrency;
using System.Reactive.Linq;
using AwesomeAssertions;
using DroidNet.Aura.Dialogs;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.SceneInspector.Components.Cameras;
using Oxygen.Editor.SceneInspector.Components.Instances;
using Oxygen.Editor.SceneInspector.SceneSettings;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Unit.Tests;

/// <summary>
/// The Scene Instance section and the instanced starting camera (scene instances 6.5, 6.10, 8.5, 8.12): the source
/// picker offers models and the scenes that would not instance this one; a swap without changes switches at once, one
/// with changes asks whether to keep them by name or discard them; a refused swap says why; the Play section lists
/// the cameras instances bring in under their instance root, and keeps a lost one with the caution glyph.
/// </summary>
[TestClass]
public sealed class SceneInstanceSectionTests
{
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Vehicles/Car.glb");
    private static readonly AssetPath Van = AssetPath.Parse("/Content/Vehicles/Van.glb");
    private static readonly AssetPath Watchtower = AssetPath.Parse("/Content/Prefabs/Watchtower.oscene");
    private static readonly NodePath CarBody = NodePath.Of(NodeId.New());
    private static readonly NodePath CarRig = NodePath.Of(NodeId.New());
    private static readonly NodePath CarLens = NodePath.Of(NodeId.New());
    private static readonly NodePath VanBody = NodePath.Of(NodeId.New());

    [TestMethod]
    public async Task AnInstanceWithoutChanges_SwapsWithoutAsking()
    {
        using var harness = Harness.Open(changed: false);
        using var section = harness.Section();

        await section.SwapAsync(Van).ConfigureAwait(false);

        _ = harness.Instance.Source.Should().Be(Van);
        _ = (section.SourceLabel, section.SourceKind, section.ChangeCount).Should().Be(("Van.glb", "Model", "No changes"));
        harness.Dialogs.VerifyNoOtherCalls();
    }

    [TestMethod]
    public async Task AnInstanceWithChanges_AsksAndKeepsThemByName()
    {
        using var harness = Harness.Open(changed: true);
        using var section = harness.Section();
        harness.Answer(DialogButton.Primary);
        _ = section.ChangeCount.Should().Be("1 change");

        await section.SwapAsync(Van).ConfigureAwait(false);

        _ = harness.Instance.Source.Should().Be(Van);
        _ = harness.Instance.Changes.Nodes.Keys.Should().Equal([VanBody], "Body is found by its name in Van.glb");
        harness.Dialogs.Verify(dialogs => dialogs.ShowAsync(It.IsAny<DialogSpec>(), It.IsAny<CancellationToken>()), Times.Once);
    }

    [TestMethod]
    public async Task DiscardingTheChanges_SwapsWithoutThem_AndCancellingSwapsNothing()
    {
        using var harness = Harness.Open(changed: true);
        using var section = harness.Section();

        harness.Answer(DialogButton.Close);
        await section.SwapAsync(Van).ConfigureAwait(false);
        _ = harness.Instance.Source.Should().Be(Car, "the user cancelled");

        harness.Answer(DialogButton.Secondary);
        await section.SwapAsync(Van).ConfigureAwait(false);
        _ = harness.Instance.Source.Should().Be(Van);
        _ = harness.Instance.Changes.IsEmpty.Should().BeTrue();
    }

    [TestMethod]
    public async Task KeepingChangesInASourceNotRead_ReadsItFirst()
    {
        using var harness = Harness.Open(changed: true, publishVan: false);
        using var section = harness.Section();
        harness.Answer(DialogButton.Primary);

        await section.SwapAsync(Van).ConfigureAwait(false);

        _ = harness.Instance.Source.Should().Be(Van);
        _ = harness.Instance.Changes.Nodes.Keys.Should().Equal([VanBody]);
    }

    [TestMethod]
    public async Task ASourceThatInstancesTheScene_IsRefusedWithItsMessage()
    {
        using var harness = Harness.Open(changed: false);
        harness.References.Setup(references => references.Of(Watchtower)).Returns([harness.SceneAsset]);
        using var section = harness.Section();

        await section.SwapAsync(Watchtower).ConfigureAwait(false);

        _ = harness.Instance.Source.Should().Be(Car);
        _ = section.ErrorMessage.Should().Contain("cannot instance itself");
    }

    [TestMethod]
    public async Task ThePicker_OffersModelsAndTheScenesThatWouldNotInstanceThisOne()
    {
        var harbor = AssetPath.Parse("/Content/Harbor.oscene");
        using var harness = Harness.Open(changed: false, [Model(Van), Scene(Watchtower), Scene(harbor)]);
        harness.References.Setup(references => references.Of(harbor)).Returns([harness.SceneAsset]);
        using var section = harness.Section();

        await section.PrepareChoicesAsync().ConfigureAwait(false);

        _ = section.SourceChoices.Groups.SelectMany(static group => group.Rows).Select(static row => row.Choice.Asset)
            .Should().BeEquivalentTo([Van, Watchtower]);
    }

    [TestMethod]
    public async Task AnInstancedScene_SaysItsLevelSettingsDoNotApply()
    {
        using var harness = Harness.Open(changed: false);
        harness.Structures.Publish(Structure(Watchtower, (VanBody, "Body", null, false)));
        using var section = harness.Section();
        _ = section.IsPrefab.Should().BeFalse("a model has no level settings");

        await section.SwapAsync(Watchtower).ConfigureAwait(false);

        _ = (section.SourceKind, section.IsPrefab).Should().Be(("Scene", true));
    }

    [TestMethod]
    public async Task ThePlaySection_ListsInstancedCameras_AndKeepsALostOneWithTheCaution()
    {
        using var harness = Harness.Open(changed: false);
        using var settings = new SceneSettingsViewModel(() => harness.Scene.Authoring, session: harness.Session, structures: harness.Structures);
        settings.SetScene(harness.Scene.Scene);
        var play = settings.Play;
        var lens = new NodeReference(harness.Root.Id, CarLens);

        _ = play.CameraChoices.Select(static choice => choice.Name).Should().Equal("Automatic", "Car3 \u203a Rig \u203a Lens");
        _ = (play.ShowsAutomaticCamera, play.AutomaticCameraName).Should().Be((true, "Car3 \u203a Rig \u203a Lens"), "Play starts through the only camera");
        play.SelectedCamera = play.CameraChoices.Single(choice => choice.Id == lens);
        await play.Pending.ConfigureAwait(false);
        _ = harness.Scene.Scene.StartingCamera.Should().Be(lens);

        using var section = harness.Section();
        await section.SwapAsync(Van).ConfigureAwait(false);
        settings.Refresh();

        var lost = play.CameraChoices.Should().ContainSingle(choice => choice.Id == lens).Subject;
        _ = lost.IsMissing.Should().BeTrue();
        _ = play.CameraError.Should().Contain("Car3");
    }

    [TestMethod]
    public async Task TheStartingCameraToggle_MakesAnInstancedCameraTheStartingCamera()
    {
        using var harness = Harness.Open(changed: false);
        using var toggle = new StartingCameraToggle(() => harness.Scene.Authoring);
        var lens = new NodeReference(harness.Root.Id, CarLens);
        toggle.Bind(harness.Scene.Scene, [lens]);

        toggle.IsChecked = true;
        await toggle.Pending.ConfigureAwait(false);

        _ = harness.Scene.Scene.StartingCamera.Should().Be(lens);
    }

    private static AssetEntry Model(AssetPath asset)
        => Entry(asset, AssetKind.Model, AssetState.Source) with { ImportSource = asset };

    private static AssetEntry Scene(AssetPath asset) => Entry(asset, AssetKind.Scene, AssetState.Descriptor);

    private static AssetEntry Entry(AssetPath asset, AssetKind kind, AssetState state)
    {
        var file = FileSystemPath.From("C:/Project" + asset.VirtualPath);
        return new(asset, asset.Name, kind, state, DerivedState: null, AssetRuntimeAvailability.Unknown, asset.VirtualPath, file, file, CookedPath: null, AssetGuid: null, DiagnosticCodes: [], IsSelectable: true);
    }

    private static SceneStructure Structure(AssetPath source, params (NodePath Path, string Name, NodePath? Parent, bool HasCamera)[] nodes)
    {
        var scene = new StructureScene(Guid.NewGuid(), source.VirtualPath);
        return new SceneStructure(
            source,
            scene,
            [.. nodes.Select(node => new StructureNode(node.Path, node.Name, node.Parent, scene, Instance: null, Geometry: null, node.HasCamera))],
            []);
    }

    /// <summary>
    /// Harbor with Car3, an instance of Car.glb (Body, and Rig holding the camera Lens), maybe with a change of Body;
    /// Van.glb holds Body.
    /// </summary>
    private sealed class Harness : IDisposable
    {
        private Harness(TestScene scene, TestSession session, FakeSceneStructures structures, Mock<IAuthoredReferences> references, IReadOnlyList<AssetEntry> assets)
        {
            this.Scene = scene;
            this.References = references;
            this.Session = session;
            this.Structures = structures;
            this.Assets = assets;
            this.Root = scene.Scene.RootNodes.Single();
        }

        public TestScene Scene { get; }

        public TestSession Session { get; }

        public FakeSceneStructures Structures { get; }

        public Mock<IAuthoredReferences> References { get; }

        public Mock<IDialogService> Dialogs { get; } = new(MockBehavior.Strict);

        public Mock<IInstancedNodeReader> Nodes { get; } = new(MockBehavior.Strict);

        public SceneNode Root { get; }

        public SceneInstanceComponent Instance => this.Root.Components.OfType<SceneInstanceComponent>().Single();

        public AssetPath SceneAsset => this.Scene.Authoring.Asset!;

        private IReadOnlyList<AssetEntry> Assets { get; }

        public static Harness Open(bool changed, IReadOnlyList<AssetEntry>? assets = null, bool publishVan = true)
        {
            var structures = new FakeSceneStructures();
            structures.Publish(Structure(Car, (CarBody, "Body", null, false), (CarRig, "Rig", null, false), (CarLens, "Lens", CarRig, true)));
            var van = Structure(Van, (VanBody, "Body", null, false));
            if (publishVan)
            {
                structures.Publish(van);
            }

            var built = new Scene { Name = "Harbor" };
            var root = new SceneNode(built, "Car3");
            built.RootNodes.Add(root);
            root.Components.Add(new SceneInstanceComponent
            {
                Name = "Scene Instance",
                Source = Car,
                Changes = changed
                    ? InstanceChanges.None.With(CarBody, InstancedNodeChanges.None.With(SourceOverrides.ChangesOf(SceneSchema.Transform.Kind).With(SceneSchema.Transform.PositionX, 2f)))
                    : InstanceChanges.None,
            });
            var references = new Mock<IAuthoredReferences>();
            _ = references.Setup(saved => saved.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
            _ = references.Setup(saved => saved.Of(It.IsAny<AssetPath>())).Returns([]);
            var scene = TestScenes.Open(built, references: references.Object, structures: structures);
            var opened = new Harness(scene, new TestSession(scene.Project), structures, references, assets ?? []);
            _ = opened.Nodes.Setup(nodes => nodes.ReadStructureAsync(It.IsAny<ProjectContext>(), Van, It.IsAny<CancellationToken>()))
                .Callback(() => structures.Publish(van))
                .ReturnsAsync(van);
            return opened;
        }

        public SceneInstanceViewModel Section()
        {
            var feed = new Mock<IAssetFeed>();
            _ = feed.SetupGet(value => value.Items).Returns(Observable.Return(this.Assets));
            var section = new SceneInstanceViewModel(
                () => this.Scene.Authoring,
                this.Session,
                ImmediateScheduler.Instance,
                new InspectorInstanceServices(feed.Object, this.References.Object, this.Structures, this.Nodes.Object, this.Dialogs.Object));
            section.UpdateValues([this.Root]);
            return section;
        }

        public void Answer(DialogButton button)
            => this.Dialogs.Setup(dialogs => dialogs.ShowAsync(It.IsAny<DialogSpec>(), It.IsAny<CancellationToken>())).ReturnsAsync(button);

        public void Dispose()
        {
            this.Scene.Dispose();
            this.Session.Dispose();
        }
    }
}
