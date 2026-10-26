// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Scenes.Tests.Interaction;

/// <summary>Node lock and visibility persistence, reload and scene activation within a project session.</summary>
[TestClass]
public sealed partial class SceneNodeInteractionTests
{
    [TestMethod]
    public async Task RestoreAsync_LoadsStoredHiddenAndLock()
    {
        using var session = OpenSession();
        var sceneId = SceneId.New();
        var hidden = NodeId.New();
        var locked = NodeId.New();

        var settings = new Mock<IEditorSettingsManager>(MockBehavior.Loose);
        var stored = new SceneNodeInteraction.ProjectInteraction(
            session.Context.ProjectId,
            new Dictionary<SceneId, SceneNodeInteraction.SceneInteraction>
            {
                [sceneId] = new([NodeReference.To(hidden)], [NodeReference.To(locked)], SceneCategories.All),
            });
        _ = settings
            .Setup(s => s.LoadSettingAsync(
                It.IsAny<SettingKey<SceneNodeInteraction.ProjectInteraction>>(),
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .ReturnsAsync(stored);

        var service = new SceneNodeInteraction(session, settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await service.RestoreAsync(sceneId).ConfigureAwait(false);

        _ = service.IsHidden(NodeReference.To(hidden)).Should().BeTrue("a stored hidden id reloads on scene restore");
        _ = service.IsLocked(NodeReference.To(locked)).Should().BeTrue("a stored locked id reloads on scene restore");
    }

    [TestMethod]
    public async Task GetLockOwner_ResolvesNearestAncestorOnlyInActiveScene()
    {
        using var session = OpenSession();
        var scene = CreateScene();
        var parent = new SceneNode(scene, "Locked Parent");
        var child = new SceneNode(scene, "Child");
        parent.Hierarchy.AddChild(child);
        scene.RootNodes.Add(parent);
        var otherSceneNode = new SceneNode(CreateScene(), "Other Scene Node");

        var settings = new Mock<IEditorSettingsManager>(MockBehavior.Loose);
        var service = new SceneNodeInteraction(session, settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await service.RestoreAsync(scene.Id).ConfigureAwait(false);
        service.SetLocked(NodeReference.To(parent.Id), isLocked: true);

        _ = service.GetLockOwner(child).Should().BeSameAs(parent);
        _ = service.GetLockOwner(parent).Should().BeSameAs(parent);
        _ = service.GetLockOwner(otherSceneNode).Should().BeNull("an inactive scene must not inherit the active scene's lock state");
    }

    [TestMethod]
    public async Task RestoreAsync_IgnoresAnotherProjectsRecord()
    {
        using var session = OpenSession();
        var sceneId = SceneId.New();
        var hidden = NodeId.New();

        var settings = new Mock<IEditorSettingsManager>(MockBehavior.Loose);

        // Same path-scope but a different ProjectId: the settings scope is reused by path, so a
        // copied project must not inherit a stranger's hidden set.
        var stored = new SceneNodeInteraction.ProjectInteraction(
            Guid.NewGuid(),
            new Dictionary<SceneId, SceneNodeInteraction.SceneInteraction>
            {
                [sceneId] = new([NodeReference.To(hidden)], [], SceneCategories.All),
            });
        _ = settings
            .Setup(s => s.LoadSettingAsync(
                It.IsAny<SettingKey<SceneNodeInteraction.ProjectInteraction>>(),
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .ReturnsAsync(stored);

        var service = new SceneNodeInteraction(session, settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await service.RestoreAsync(sceneId).ConfigureAwait(false);

        _ = service.IsHidden(NodeReference.To(hidden)).Should().BeFalse("a record whose ProjectId differs is rejected");
    }

    [TestMethod]
    public async Task SceneSwitchAndRestart_PreservesIndependentStates()
    {
        using var session = OpenSession();
        var sceneA = SceneId.New();
        var sceneB = SceneId.New();
        var nodeA = NodeId.New();
        var nodeB = NodeId.New();
        var store = new Dictionary<SettingContext, SceneNodeInteraction.ProjectInteraction>();
        var settings = CreateStore(store);
        var service = new SceneNodeInteraction(session, settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await service.RestoreAsync(sceneA).ConfigureAwait(false);
        service.SetHidden(NodeReference.To(nodeA), isHidden: true);
        service.SetCategories(new SceneCategories(ShowMeshes: false, ShowLights: true, ShowCameras: false));
        await service.RestoreAsync(sceneB).ConfigureAwait(false);
        _ = service.HiddenNodes().Should().BeEmpty();
        _ = service.Categories.Should().Be(SceneCategories.All);
        service.SetLocked(NodeReference.To(nodeB), isLocked: true);
        service.SetCategories(new SceneCategories(ShowMeshes: true, ShowLights: false, ShowCameras: true));
        await service.RestoreAsync(sceneA).ConfigureAwait(false);

        _ = service.IsHidden(NodeReference.To(nodeA)).Should().BeTrue();
        _ = service.IsLocked(NodeReference.To(nodeB)).Should().BeFalse();
        _ = service.Categories.Should().Be(new SceneCategories(ShowMeshes: false, ShowLights: true, ShowCameras: false));
        _ = store[session.Context.SettingsScope].Scenes.Keys.Should().BeEquivalentTo([sceneA, sceneB]);
        var restarted = new SceneNodeInteraction(session, settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await restarted.RestoreAsync(sceneB).ConfigureAwait(false);
        _ = restarted.IsLocked(NodeReference.To(nodeB)).Should().BeTrue();
        _ = restarted.IsHidden(NodeReference.To(nodeA)).Should().BeFalse();
        _ = restarted.Categories.Should().Be(new SceneCategories(ShowMeshes: true, ShowLights: false, ShowCameras: true));
    }

    [TestMethod]
    public async Task SessionsOfTwoProjects_KeepTheirOwnState()
    {
        using var first = OpenSession("First");
        using var second = OpenSession("Second");
        var sceneId = SceneId.New();
        var node = NodeId.New();
        var store = new Dictionary<SettingContext, SceneNodeInteraction.ProjectInteraction>();
        var settings = CreateStore(store);

        var firstService = new SceneNodeInteraction(first, settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await firstService.RestoreAsync(sceneId).ConfigureAwait(false);
        firstService.SetHidden(NodeReference.To(node), isHidden: true);

        var secondService = new SceneNodeInteraction(second, settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await secondService.RestoreAsync(sceneId).ConfigureAwait(false);
        _ = secondService.IsHidden(NodeReference.To(node)).Should().BeFalse("another project's session has its own state");
        secondService.SetLocked(NodeReference.To(node), isLocked: true);
        await secondService.RestoreAsync(sceneId).ConfigureAwait(false);

        var reopened = new SceneNodeInteraction(first, settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await reopened.RestoreAsync(sceneId).ConfigureAwait(false);
        _ = reopened.IsHidden(NodeReference.To(node)).Should().BeTrue();
        _ = reopened.IsLocked(NodeReference.To(node)).Should().BeFalse("the other session's write does not reach this project");
        _ = store.Keys.Should().BeEquivalentTo([first.Context.SettingsScope, second.Context.SettingsScope]);
    }

    [TestMethod]
    public async Task MountChange_KeepsTheProjectsState()
    {
        using var session = OpenSession();
        var sceneId = SceneId.New();
        var node = NodeId.New();
        var store = new Dictionary<SettingContext, SceneNodeInteraction.ProjectInteraction>();
        var settings = CreateStore(store);
        var service = new SceneNodeInteraction(session, settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await service.RestoreAsync(sceneId).ConfigureAwait(false);
        var before = session.Context;

        // A mount change replaces the context with the same project id and root (scope topology, decision 7).
        session.ChangeContext(before with { CookedContentOrder = [] });
        service.SetHidden(NodeReference.To(node), isHidden: true);
        await service.RestoreAsync(sceneId).ConfigureAwait(false);

        _ = service.IsHidden(NodeReference.To(node)).Should().BeTrue();
        _ = store.Keys.Should().Equal([before.SettingsScope], "the project's settings scope is its root, which a mount change keeps");
    }

    [TestMethod]
    public async Task InvalidSavedState_PublishesFailureAndKeepsDefaults()
    {
        using var session = OpenSession();
        var settings = new Mock<IEditorSettingsManager>();
        _ = settings.Setup(value => value.LoadSettingAsync(
                SceneNodeInteraction.Key,
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .ReturnsAsync(new SceneNodeInteraction.ProjectInteraction(session.Context.ProjectId, null!));
        var results = new CapturingOperationResultPublisher();
        var service = new SceneNodeInteraction(session, settings.Object, results, new OperationStatusReducer());

        await service.RestoreAsync(SceneId.New()).ConfigureAwait(false);

        _ = service.HiddenNodes().Should().BeEmpty();
        _ = service.Categories.Should().Be(SceneCategories.All);
        _ = results.Published.SelectMany(result => result.Diagnostics).Should()
            .Contain(diagnostic => diagnostic.Code == DiagnosticCodes.SettingsPrefix + "INVALID_STATE");
    }

    [TestMethod]
    public async Task StaleRestoreAfterClear_DoesNotReapplyPriorState()
    {
        using var session = OpenSession();
        var sceneId = SceneId.New();
        var hidden = NodeId.New();
        var load = new TaskCompletionSource<SceneNodeInteraction.ProjectInteraction?>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var settings = new Mock<IEditorSettingsManager>();
        _ = settings.Setup(value => value.LoadSettingAsync(
                SceneNodeInteraction.Key,
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .Returns(load.Task);
        var service = new SceneNodeInteraction(session, settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        var restoration = service.RestoreAsync(sceneId);
        service.ClearActiveScene();
        load.SetResult(new SceneNodeInteraction.ProjectInteraction(
            session.Context.ProjectId,
            new Dictionary<SceneId, SceneNodeInteraction.SceneInteraction>
            {
                [sceneId] = new([NodeReference.To(hidden)], [], new SceneCategories(ShowMeshes: false, ShowLights: false, ShowCameras: false)),
            }));

        await restoration.ConfigureAwait(false);

        _ = service.ActiveSceneId.Should().Be(SceneId.Empty);
        _ = service.IsHidden(NodeReference.To(hidden)).Should().BeFalse();
        _ = service.Categories.Should().Be(SceneCategories.All);
    }

    [TestMethod]
    public async Task LocksOfInstancedNodesSurviveARestart()
    {
        using var session = OpenSession();
        var sceneId = SceneId.New();
        var door = new NodeReference(NodeId.New(), NodePath.Of(NodeId.New(), NodeId.New()));
        var store = new Dictionary<SettingContext, SceneNodeInteraction.ProjectInteraction>();
        var settings = CreateStore(store);
        var service = new SceneNodeInteraction(session, settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await service.RestoreAsync(sceneId).ConfigureAwait(false);

        service.SetLocked(door, isLocked: true);
        var restarted = new SceneNodeInteraction(session, settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await restarted.RestoreAsync(sceneId).ConfigureAwait(false);

        _ = restarted.IsLocked(door).Should().BeTrue("settings LLD 5.1: an instanced node is locked by its instance root and node path");
        _ = restarted.IsLocked(NodeReference.To(door.Node)).Should().BeFalse("locking an instanced node does not lock its instance root");
    }

    [TestMethod]
    public async Task GetLockOwner_OfAnInstancedNodeChecksItsPathThenTheInstanceRootsAncestry()
    {
        using var session = OpenSession();
        var scene = CreateScene();
        var group = new SceneNode(scene, "Group");
        var car = new SceneNode(scene, "Car");
        group.Hierarchy.AddChild(car);
        scene.RootNodes.Add(group);
        var wheel = new NodeReference(car.Id, NodePath.Of(NodeId.New()));
        var rim = new NodeReference(car.Id, wheel.Path.Append(NodeId.New()));
        var service = new SceneNodeInteraction(session, new Mock<IEditorSettingsManager>().Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await service.RestoreAsync(scene.Id).ConfigureAwait(false);

        _ = service.GetLockOwner(scene, rim).Should().BeNull();

        service.SetLocked(NodeReference.To(group.Id), isLocked: true);
        _ = service.GetLockOwner(scene, rim).Should().Be(NodeReference.To(group.Id), "an ancestor of the instance root locks what the instance brought in");

        service.SetLocked(wheel, isLocked: true);
        _ = service.GetLockOwner(scene, rim).Should().Be(wheel, "the closest locked node owns the lock");
        _ = service.GetLockOwner(CreateScene(), rim).Should().BeNull("an inactive scene must not inherit the active scene's lock state");
    }

    [TestMethod]
    public async Task DropUnresolvedNodes_DropsOnlyThePathsTheSourceNoLongerHolds()
    {
        using var session = OpenSession();
        var sceneId = SceneId.New();
        var root = NodeId.New();
        var kept = new NodeReference(root, NodePath.Of(NodeId.New()));
        var gone = new NodeReference(root, NodePath.Of(NodeId.New()));
        var otherInstance = new NodeReference(NodeId.New(), gone.Path);
        var store = new Dictionary<SettingContext, SceneNodeInteraction.ProjectInteraction>();
        var service = new SceneNodeInteraction(session, CreateStore(store).Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await service.RestoreAsync(sceneId).ConfigureAwait(false);
        foreach (var node in new[] { kept, gone, otherInstance, NodeReference.To(root) })
        {
            service.SetLocked(node, isLocked: true);
            service.SetHidden(node, isHidden: true);
        }

        var changes = 0;
        service.StateChanged += (_, _) => changes++;
        service.DropUnresolvedNodes(root, path => path.Equals(kept.Path));

        _ = service.IsLocked(gone).Should().BeFalse("a swap, reimport or removed change took its node away");
        _ = service.IsHidden(gone).Should().BeFalse("the Eye of a node that is gone goes with it");
        _ = service.IsLocked(kept).Should().BeTrue();
        _ = service.IsHidden(kept).Should().BeTrue();
        _ = service.IsLocked(otherInstance).Should().BeTrue("another instance's nodes are not this structure's");
        _ = service.IsHidden(otherInstance).Should().BeTrue();
        _ = service.IsLocked(NodeReference.To(root)).Should().BeTrue("the instance root is a node of the scene");
        _ = service.IsHidden(NodeReference.To(root)).Should().BeTrue();
        _ = changes.Should().Be(1);
        _ = store[session.Context.SettingsScope].Scenes[sceneId].LockedNodeIds.Should().NotContain(gone);
        _ = store[session.Context.SettingsScope].Scenes[sceneId].HiddenNodeIds.Should().NotContain(gone);

        service.DropUnresolvedNodes(root, _ => true);
        _ = changes.Should().Be(1, "nothing dropped, nothing changed");
    }

    [TestMethod]
    public async Task ClearActiveScene_ResetsCategoriesWithoutSavingEmptyScene()
    {
        using var session = OpenSession();
        var settings = new Mock<IEditorSettingsManager>();
        var service = new SceneNodeInteraction(session, settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        await service.RestoreAsync(SceneId.New()).ConfigureAwait(false);
        service.SetCategories(new SceneCategories(ShowMeshes: false, ShowLights: false, ShowCameras: false));

        service.ClearActiveScene();

        _ = service.ActiveSceneId.Should().Be(SceneId.Empty);
        _ = service.Categories.Should().Be(SceneCategories.All);
        _ = service.HiddenNodes().Should().BeEmpty();
    }

    private static Scene CreateScene() => new() { Name = "Test Scene" };

    private static TestSession OpenSession(string name = "Project")
        => new(TestProject.Create(new MockFileSystem(), Path.Combine(Path.GetTempPath(), "Oxygen.SceneInteraction", name)).Context);

    // A settings store that keeps one interaction record per settings scope.
    private static Mock<IEditorSettingsManager> CreateStore(Dictionary<SettingContext, SceneNodeInteraction.ProjectInteraction> store)
    {
        var settings = new Mock<IEditorSettingsManager>();
        _ = settings.Setup(value => value.LoadSettingAsync(
                SceneNodeInteraction.Key,
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .Returns((SettingKey<SceneNodeInteraction.ProjectInteraction> _, SettingContext context, IProgress<SettingsProgress> _, CancellationToken _)
                => Task.FromResult(store.GetValueOrDefault(context)));
        _ = settings.Setup(value => value.SaveSettingAsync(
                SceneNodeInteraction.Key,
                It.IsAny<SceneNodeInteraction.ProjectInteraction>(),
                It.IsAny<SettingContext>(),
                It.IsAny<IProgress<SettingsProgress>>(),
                It.IsAny<CancellationToken>()))
            .Callback((
                SettingKey<SceneNodeInteraction.ProjectInteraction> _,
                SceneNodeInteraction.ProjectInteraction value,
                SettingContext context,
                IProgress<SettingsProgress> _,
                CancellationToken _) => store[context] = value)
            .Returns(Task.CompletedTask);
        return settings;
    }

    private sealed class CapturingOperationResultPublisher : IOperationResultPublisher
    {
        public List<OperationResult> Published { get; } = [];

        public void Publish(OperationResult result) => this.Published.Add(result);

        public IDisposable Subscribe(IObserver<OperationResult> observer) => new NoopDisposable();
    }

    private sealed partial class NoopDisposable : IDisposable
    {
        public void Dispose()
        {
        }
    }
}
