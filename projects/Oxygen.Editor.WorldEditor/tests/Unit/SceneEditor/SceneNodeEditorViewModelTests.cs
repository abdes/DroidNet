// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Engine;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Workspace;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneEditor;

[TestClass]
[TestCategory("Inspector")]
public sealed class SceneNodeEditorViewModelTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public void Constructor_WhenActiveSceneHasPendingPropertySyncs_ShowsEditorLevelPendingBanner()
    {
        var scene = CreateScene();
        var node = CreateNode(scene);
        var sync = new Mock<ISceneEngineSync>();
        _ = sync
            .Setup(service => service.GetPendingPropertySyncCount(scene.Id))
            .Returns(2);

        using var tab = SceneTabs.Open(node.Scene);
        using var sut = CreateSut(sync.Object, tab, [node]);

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
        var interaction = new WorkspaceInteractionService(
            new Mock<IEditorSettingsManager>(MockBehavior.Loose).Object,
            Mock.Of<IOperationResultPublisher>(),
            new OperationStatusReducer());
        await interaction.RestoreAsync(
            ProjectContext.FromProjectInfo(new ProjectInfo("Inspector lock tests", Category.Games, "H:/InspectorLockTests")),
            scene.Id).ConfigureAwait(false);
        interaction.SetLocked(parent.Id, isLocked: true);

        using var tab = SceneTabs.Open(child.Scene);
        using var sut = CreateSut(Mock.Of<ISceneEngineSync>(), tab, [child], interaction);

        _ = sut.SelectedNode!.Id.Should().Be(child.Id);
        _ = sut.HasLockedSelection.Should().BeTrue();
        _ = sut.CanEditSelectedNodes.Should().BeFalse();
    }

    [TestMethod]
    public void PendingPropertySyncCountChanged_WhenActiveSceneMatches_UpdatesEditorLevelPendingBanner()
    {
        var scene = CreateScene();
        var node = CreateNode(scene);
        var sync = new Mock<ISceneEngineSync>();
        _ = sync
            .Setup(service => service.GetPendingPropertySyncCount(scene.Id))
            .Returns(0);
        using var tab = SceneTabs.Open(node.Scene);
        using var sut = CreateSut(sync.Object, tab, [node]);

        sync.Raise(
            service => service.PendingPropertySyncCountChanged += null,
            new PendingPropertySyncCountChangedEventArgs(scene.Id, pendingCount: 1));

        _ = sut.PendingLiveSyncEditCount.Should().Be(1);
        _ = sut.HasPendingLiveSyncEdits.Should().BeTrue();
        _ = sut.PendingLiveSyncMessage.Should().Be("1 editor property edit will replay after the scene syncs.");
    }

    [TestMethod]
    public void PendingPropertySyncCountChanged_WhenQueueIsEmpty_HidesEditorLevelPendingBanner()
    {
        var scene = CreateScene();
        var node = CreateNode(scene);
        var sync = new Mock<ISceneEngineSync>();
        _ = sync
            .Setup(service => service.GetPendingPropertySyncCount(scene.Id))
            .Returns(1);
        using var tab = SceneTabs.Open(node.Scene);
        using var sut = CreateSut(sync.Object, tab, [node]);

        sync.Raise(
            service => service.PendingPropertySyncCountChanged += null,
            new PendingPropertySyncCountChangedEventArgs(scene.Id, pendingCount: 0));

        _ = sut.PendingLiveSyncEditCount.Should().Be(0);
        _ = sut.HasPendingLiveSyncEdits.Should().BeFalse();
    }

    [TestMethod]
    public void PendingPropertySyncCountChanged_WhenSceneDiffers_IgnoresEvent()
    {
        var scene = CreateScene();
        var node = CreateNode(scene);
        var sync = new Mock<ISceneEngineSync>();
        _ = sync
            .Setup(service => service.GetPendingPropertySyncCount(scene.Id))
            .Returns(1);
        using var tab = SceneTabs.Open(node.Scene);
        using var sut = CreateSut(sync.Object, tab, [node]);

        sync.Raise(
            service => service.PendingPropertySyncCountChanged += null,
            new PendingPropertySyncCountChangedEventArgs(SceneId.New(), pendingCount: 0));

        _ = sut.PendingLiveSyncEditCount.Should().Be(1);
        _ = sut.HasPendingLiveSyncEdits.Should().BeTrue();
    }

    [TestMethod]
    public async Task DirectionalLightEditor_WhenSunDirectionChanges_EditsTransformRotation()
    {
        var scene = CreateScene();
        var built = CreateDirectionalLightNode(scene);
        using var tab = SceneTabs.Open(scene);
        var node = tab.NodeOf(built);
        var before = node.Components.OfType<TransformComponent>().Single().LocalRotation;

        using var sut = new DirectionalLightViewModel(() => tab.Authoring);
        sut.UpdateValues([node]);
        sut.SunAzimuth = 45f;
        await sut.PendingEdits.ConfigureAwait(false);

        _ = node.Components.OfType<TransformComponent>().Single().LocalRotation.Should().NotBe(before);
        _ = tab.Document.History.UndoStack.Should().ContainSingle();
    }

    private static SceneNodeEditorViewModel CreateSut(
        ISceneEngineSync sceneEngineSync,
        SceneTab tab,
        IList<SceneNode> selectedNodes,
        WorkspaceInteractionService? interaction = null)
    {
        // The document edits its own copy of the scene, so the selection holds the document's nodes. They show no
        // components: the component editors need a window.
        var nodes = selectedNodes.Select(tab.NodeOf).ToArray();
        foreach (var node in nodes)
        {
            node.Components.Clear();
        }

        var messenger = new StrongReferenceMessenger();
        messenger.Register<SceneNodeSelectionRequestMessage>(
            recipient: new object(),
            handler: (_, message) => message.Reply(nodes));

        var documents = new TestDocumentArea(tab.Document);
        var selectionService = new SceneSelectionService();
        selectionService.SetSelection(tab.DocumentId, nodes, "Test");

        return new SceneNodeEditorViewModel(
            new HostingContext
            {
                Dispatcher = null!,
                Application = null!,
                DispatcherScheduler = null!,
            },
            new ViewModelToView(new Mock<IViewLocator>().Object),
            messenger,
            documents,
            default,
            Mock.Of<IAssetFeed>(feed => feed.Items == Observable.Never<IReadOnlyList<AssetEntry>>()),
            sceneEngineSync,
            selectionService,
            new Testing.BuiltinCatalogDiscoveryFixture(),
            Mock.Of<ISceneContentDemandService>(),
            Mock.Of<ContentPipeline.Inspection.IGeometryMaterialSlotProvider>(),
            Mock.Of<IProjectContextService>(),
            interaction: interaction);
    }

    private static Scene CreateScene()
    {
        var project = new Mock<IProject>().Object;
        return new Scene(project) { Name = "Test Scene" };
    }

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
