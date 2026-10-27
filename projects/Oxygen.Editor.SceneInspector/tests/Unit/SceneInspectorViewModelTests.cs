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
using Oxygen.Editor.Input.Testing;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.SceneInspector.Components.Lights;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.SceneInspector.Unit.Tests;

[TestClass]
[TestCategory("Inspector")]
internal sealed class SceneInspectorViewModelTests
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
        using var surfaces = new FakeSurfaceProvider();
        using var sut = CreateSut(preview.Object, surfaces, testScene, [node]);

        _ = sut.PendingLiveSyncEditCount.Should().Be(2);
        _ = sut.HasPendingLiveSyncEdits.Should().BeTrue();
        _ = sut.PendingLiveSyncTitle.Should().Be("Runtime sync pending");
        _ = sut.PendingLiveSyncMessage.Should().Be("2 editor property edits will replay after the scene syncs.");
    }

    [TestMethod]
    public async Task LockedSelection_RemainsInspectableAndDisablesAuthoring()
    {
        var scene = CreateScene();
        var parent = new SceneNode(scene, "Locked Parent");
        var child = new SceneNode(scene, "Child");
        parent.Hierarchy.AddChild(child);
        scene.RootNodes.Add(parent);
        var project = TestProject.Create(new MockFileSystem());
        using var session = new TestSession(project.Context);
        var interaction = new SceneInteractionKit().Create(session);
        await interaction.RestoreAsync(scene.Id).ConfigureAwait(false);
        interaction.SetLocked(NodeReference.To(parent.Id), isLocked: true);

        using var testScene = TestScenes.Open(child.Scene, project: project);
        using var surfaces = new FakeSurfaceProvider();
        using var sut = CreateSut(Mock.Of<IScenePreview>(), surfaces, testScene, [child], interaction);

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
        using var surfaces = new FakeSurfaceProvider();
        using var sut = CreateSut(preview.Object, surfaces, testScene, [node]);
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
        using var surfaces = new FakeSurfaceProvider();
        using var sut = CreateSut(preview.Object, surfaces, testScene, [node]);
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
        using var surfaces = new FakeSurfaceProvider();
        using var sut = CreateSut(preview.Object, surfaces, testScene, [node]);
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
        var before = node.Transform.LocalRotation;

        using var sut = new DirectionalLightViewModel(() => testScene.Authoring);
        sut.UpdateValues([node]);
        sut.SunAzimuth = 45f;
        await sut.PendingEdits.ConfigureAwait(false);

        _ = node.Transform.LocalRotation.Should().NotBe(before);
        _ = testScene.Document.History.UndoStack.Should().ContainSingle();
    }

    private static SceneInspectorViewModel CreateSut(
        IScenePreview scenePreview,
        FakeSurfaceProvider surfaces,
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
        selectionService.SetSelection(testScene.DocumentId, [.. nodes.Select(node => NodeReference.To(node.Id))], "Test");

        return new SceneInspectorViewModel(
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
            surfaces,
            Mock.Of<IProjectSession>(session => session.Context == testScene.Project),
            new FakeInputSources(),
            interaction: interaction);
    }

    private static Scene CreateScene() => new() { Name = "Test Scene" };

    private static SceneNode CreateNode(Scene scene)
    {
        var node = new SceneNode(scene, "Cube");
        scene.RootNodes.Add(node);
        return node;
    }

    private static SceneNode CreateDirectionalLightNode(Scene scene)
    {
        var node = new SceneNode(scene, "Sun");
        _ = node.AddComponent(new DirectionalLightComponent { Name = "Directional Light" });
        scene.RootNodes.Add(node);
        return node;
    }
}
