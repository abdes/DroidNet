// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using AwesomeAssertions;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Input.Testing;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneEditor;

[TestClass]
[TestCategory("Inspector")]
internal sealed class SceneNodeEditorViewModelTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public void Constructor_WhenActiveDocumentHasPendingFields_ShowsEditorLevelPendingBanner()
    {
        var scene = CreateScene();
        var node = CreateNode(scene);
        using var testScene = TestScenes.Open(node.Scene);
        var preview = new Mock<IScenePreview>();
        _ = preview.Setup(value => value.PendingFieldCount(testScene.DocumentId)).Returns(2);
        using var slots = new FakeMaterialSlotProvider();
        using var sut = CreateSut(preview.Object, slots, testScene, [node]);

        _ = sut.PendingLiveSyncEditCount.Should().Be(2);
        _ = sut.HasPendingLiveSyncEdits.Should().BeTrue();
        _ = sut.PendingLiveSyncTitle.Should().Be("Runtime sync pending");
        _ = sut.PendingLiveSyncMessage.Should().Be("2 editor property edits will replay after the scene syncs.");
    }

    [TestMethod]
    public async Task LockedSelection_RemainsInspectableAndDisablesAuthoring()
    {
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Locked Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        parent.AddChild(child);
        scene.RootNodes.Add(parent);
        var interaction = new SceneNodeInteraction(
            new Mock<IEditorSettingsManager>(MockBehavior.Loose).Object,
            Mock.Of<IOperationResultPublisher>(),
            new OperationStatusReducer());
        await interaction.RestoreAsync(
            TestProject.Create(new MockFileSystem()).Context,
            scene.Id).ConfigureAwait(false);
        interaction.SetLocked(parent.Id, isLocked: true);

        using var testScene = TestScenes.Open(child.Scene);
        using var slots = new FakeMaterialSlotProvider();
        using var sut = CreateSut(Mock.Of<IScenePreview>(), slots, testScene, [child], interaction);

        _ = sut.SelectedNode!.Id.Should().Be(child.Id);
        _ = sut.HasLockedSelection.Should().BeTrue();
        _ = sut.CanEditSelectedNodes.Should().BeFalse();
    }

    [TestMethod]
    public void PendingFieldsChanged_WhenActiveDocumentMatches_UpdatesEditorLevelPendingBanner()
    {
        var scene = CreateScene();
        var node = CreateNode(scene);
        using var testScene = TestScenes.Open(node.Scene);
        var preview = new Mock<IScenePreview>();
        _ = preview.Setup(value => value.PendingFieldCount(testScene.DocumentId)).Returns(0);
        using var slots = new FakeMaterialSlotProvider();
        using var sut = CreateSut(preview.Object, slots, testScene, [node]);
        _ = preview.Setup(value => value.PendingFieldCount(testScene.DocumentId)).Returns(1);

        preview.Raise(value => value.PendingFieldsChanged += null, preview.Object, new ScenePreviewEventArgs(testScene.DocumentId));

        _ = sut.PendingLiveSyncEditCount.Should().Be(1);
        _ = sut.HasPendingLiveSyncEdits.Should().BeTrue();
        _ = sut.PendingLiveSyncMessage.Should().Be("1 editor property edit will replay after the scene syncs.");
    }

    [TestMethod]
    public void PendingFieldsChanged_WhenNoneWait_HidesEditorLevelPendingBanner()
    {
        var scene = CreateScene();
        var node = CreateNode(scene);
        using var testScene = TestScenes.Open(node.Scene);
        var preview = new Mock<IScenePreview>();
        _ = preview.Setup(value => value.PendingFieldCount(testScene.DocumentId)).Returns(1);
        using var slots = new FakeMaterialSlotProvider();
        using var sut = CreateSut(preview.Object, slots, testScene, [node]);
        _ = preview.Setup(value => value.PendingFieldCount(testScene.DocumentId)).Returns(0);

        preview.Raise(value => value.PendingFieldsChanged += null, preview.Object, new ScenePreviewEventArgs(testScene.DocumentId));

        _ = sut.PendingLiveSyncEditCount.Should().Be(0);
        _ = sut.HasPendingLiveSyncEdits.Should().BeFalse();
    }

    [TestMethod]
    public void PendingFieldsChanged_WhenDocumentDiffers_IgnoresEvent()
    {
        var scene = CreateScene();
        var node = CreateNode(scene);
        using var testScene = TestScenes.Open(node.Scene);
        var preview = new Mock<IScenePreview>();
        _ = preview.Setup(value => value.PendingFieldCount(testScene.DocumentId)).Returns(1);
        using var slots = new FakeMaterialSlotProvider();
        using var sut = CreateSut(preview.Object, slots, testScene, [node]);
        var other = DroidNet.Documents.DocumentId.New();
        _ = preview.Setup(value => value.PendingFieldCount(other)).Returns(0);

        preview.Raise(value => value.PendingFieldsChanged += null, preview.Object, new ScenePreviewEventArgs(other));

        _ = sut.PendingLiveSyncEditCount.Should().Be(1);
        _ = sut.HasPendingLiveSyncEdits.Should().BeTrue();
    }

    [TestMethod]
    public async Task DirectionalLightEditor_WhenSunDirectionChanges_EditsTransformRotation()
    {
        var scene = CreateScene();
        var built = CreateDirectionalLightNode(scene);
        using var testScene = TestScenes.Open(scene);
        var node = testScene.NodeOf(built);
        var before = node.Components.OfType<TransformComponent>().Single().LocalRotation;

        using var sut = new DirectionalLightViewModel(() => testScene.Authoring);
        sut.UpdateValues([node]);
        sut.SunAzimuth = 45f;
        await sut.PendingEdits.ConfigureAwait(false);

        _ = node.Components.OfType<TransformComponent>().Single().LocalRotation.Should().NotBe(before);
        _ = testScene.Document.History.UndoStack.Should().ContainSingle();
    }

    private static SceneNodeEditorViewModel CreateSut(
        IScenePreview scenePreview,
        FakeMaterialSlotProvider slots,
        TestScene testScene,
        IList<SceneNode> selectedNodes,
        SceneNodeInteraction? interaction = null)
    {
        // The document edits its own copy of the scene, so the selection holds the document's nodes. They show no
        // components: the component editors need a window.
        var nodes = selectedNodes.Select(testScene.NodeOf).ToArray();
        foreach (var node in nodes)
        {
            node.Components.Clear();
        }

        var documents = new TestDocumentArea(testScene.Document);
        var selectionService = new SceneSelectionService();
        selectionService.SetSelection(testScene.DocumentId, nodes, "Test");

        return new SceneNodeEditorViewModel(
            new HostingContext
            {
                Dispatcher = null!,
                Application = null!,
                DispatcherScheduler = null!,
            },
            new ViewModelToView(new Mock<IViewLocator>().Object),
            Mock.Of<IAssetReveal>(),
            documents,
            Mock.Of<IAssetFeed>(feed => feed.Items == Observable.Never<IReadOnlyList<AssetEntry>>()),
            scenePreview,
            selectionService,
            slots,
            Mock.Of<IProjectContextService>(),
            new FakeInputSources(),
            interaction: interaction);
    }

    private static Scene CreateScene() => new() { Name = "Test Scene" };

    private static SceneNode CreateNode(Scene scene)
    {
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        return node;
    }

    private static SceneNode CreateDirectionalLightNode(Scene scene)
    {
        var node = new SceneNode(scene) { Name = "Sun" };
        _ = node.AddComponent(new DirectionalLightComponent { Name = "Directional Light" });
        scene.RootNodes.Add(node);
        return node;
    }
}
