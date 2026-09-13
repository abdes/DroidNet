// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.TimeMachine;
using Moq;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

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

        using var sut = CreateSut(sync.Object, [node]);

        _ = sut.PendingLiveSyncEditCount.Should().Be(2);
        _ = sut.HasPendingLiveSyncEdits.Should().BeTrue();
        _ = sut.PendingLiveSyncTitle.Should().Be("Runtime sync pending");
        _ = sut.PendingLiveSyncMessage.Should().Be("2 editor property edits will replay after the scene syncs.");
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
        using var sut = CreateSut(sync.Object, [node]);

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
        using var sut = CreateSut(sync.Object, [node]);

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
        using var sut = CreateSut(sync.Object, [node]);

        sync.Raise(
            service => service.PendingPropertySyncCountChanged += null,
            new PendingPropertySyncCountChangedEventArgs(Guid.NewGuid(), pendingCount: 0));

        _ = sut.PendingLiveSyncEditCount.Should().Be(1);
        _ = sut.HasPendingLiveSyncEdits.Should().BeTrue();
    }

    [TestMethod]
    public async Task DirectionalLightEditor_WhenSunDirectionChanges_EditsTransformRotation()
    {
        var scene = CreateScene();
        var node = CreateDirectionalLightNode(scene);
        var context = CreateContext(scene);
        var commandService = new Mock<ISceneDocumentCommandService>();
        var editCompletion = new TaskCompletionSource<IReadOnlyDictionary<Guid, PropertyEdit>>(TaskCreationOptions.RunContinuationsAsynchronously);
        _ = commandService
            .Setup(service => service.EditPropertiesForTargetsAsync(
                context,
                It.IsAny<IReadOnlyDictionary<Guid, PropertyEdit>>(),
                It.IsAny<string>(),
                It.IsAny<EditSessionToken>()))
            .Callback<SceneDocumentCommandContext, IReadOnlyDictionary<Guid, PropertyEdit>, string, EditSessionToken>(
                (_, edits, _, _) => editCompletion.SetResult(edits))
            .ReturnsAsync(SceneCommandResult.Success);

        using var sut = new DirectionalLightViewModel(commandService.Object, () => context);
        sut.UpdateValues([node]);
        sut.SunAzimuth = 45f;
        var edits = await editCompletion.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        var edit = edits.Should().ContainSingle().Which.Value;
        _ = edit.Ids.Should().OnlyContain(id => id.ComponentKind == SceneDocumentCommandService.TransformKind && id.Pointer.StartsWith("/local_rotation_euler_degrees/", StringComparison.Ordinal));
        _ = edit.Should().OnlyContain(pair => pair.Value is float && float.IsFinite((float)pair.Value));
    }

    private static SceneNodeEditorViewModel CreateSut(
        ISceneEngineSync sceneEngineSync,
        IList<SceneNode> selectedNodes)
    {
        var messenger = new StrongReferenceMessenger();
        messenger.Register<SceneNodeSelectionRequestMessage>(
            recipient: new object(),
            handler: (_, message) => message.Reply(selectedNodes));

        return new SceneNodeEditorViewModel(
            new HostingContext
            {
                Dispatcher = null!,
                Application = null!,
                DispatcherScheduler = null!,
            },
            new ViewModelToView(new Mock<IViewLocator>().Object),
            messenger,
            new Mock<ISceneDocumentCommandService>().Object,
            new Mock<IDocumentService>().Object,
            default,
            Oxygen.Testing.AssetStatusFixture.EmptyProvider,
            new Mock<IMaterialPickerService>().Object,
            sceneEngineSync,
            new Oxygen.Testing.BuiltinCatalogDiscoveryFixture(),
            Mock.Of<ISceneContentDemandService>());
    }

    private static Scene CreateScene()
    {
        var project = new Mock<IProject>().Object;
        return new Scene(project) { Name = "Test Scene" };
    }

    private static SceneDocumentCommandContext CreateContext(Scene scene)
    {
        var metadata = new SceneDocumentMetadata { Title = scene.Name };
        return new(metadata.DocumentId, metadata, scene, new HistoryKeeper(scene));
    }

    private static SceneNode CreateNode(Scene scene)
    {
        var node = new SceneNode(scene) { Name = "Cube" };
        node.Components.Clear();
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
