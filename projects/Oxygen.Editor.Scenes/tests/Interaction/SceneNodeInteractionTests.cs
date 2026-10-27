// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Scenes.Tests.Interaction;

/// <summary>Node lock and visibility persistence, reload and scene activation within a project session.</summary>
[TestClass]
public sealed partial class SceneNodeInteractionTests
{
    private static readonly AssetPath CarSource = AssetPath.Parse("/Content/Vehicles/Car.glb");

    [TestMethod]
    public async Task RestoreAsync_LoadsStoredHiddenAndLock()
    {
        using var session = OpenSession();
        var sceneId = SceneId.New();
        var hidden = NodeId.New();
        var locked = NodeId.New();
        var kit = new SceneInteractionKit();
        kit.Settings.Store(
            SceneNodeInteraction.Key,
            session.Context.SettingsScope,
            new SceneNodeInteraction.ProjectInteraction(
                session.Context.ProjectId,
                new Dictionary<SceneId, SceneNodeInteraction.SceneInteraction>
                {
                    [sceneId] = new([NodeReference.To(hidden)], [NodeReference.To(locked)], SceneCategories.All),
                }));
        var service = kit.Create(session);

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
        var service = new SceneInteractionKit().Create(session);
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
        var kit = new SceneInteractionKit();

        // Same path-scope but a different ProjectId: the settings scope is reused by path, so a
        // copied project must not inherit a stranger's hidden set.
        kit.Settings.Store(
            SceneNodeInteraction.Key,
            session.Context.SettingsScope,
            new SceneNodeInteraction.ProjectInteraction(
                Guid.NewGuid(),
                new Dictionary<SceneId, SceneNodeInteraction.SceneInteraction>
                {
                    [sceneId] = new([NodeReference.To(hidden)], [], SceneCategories.All),
                }));
        var service = kit.Create(session);

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
        var kit = new SceneInteractionKit();
        var service = kit.Create(session);
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
        _ = StoredFor(kit, session).Scenes.Keys.Should().BeEquivalentTo([sceneA, sceneB]);
        var restarted = kit.Create(session);
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
        var kit = new SceneInteractionKit();

        var firstService = kit.Create(first);
        await firstService.RestoreAsync(sceneId).ConfigureAwait(false);
        firstService.SetHidden(NodeReference.To(node), isHidden: true);

        var secondService = kit.Create(second);
        await secondService.RestoreAsync(sceneId).ConfigureAwait(false);
        _ = secondService.IsHidden(NodeReference.To(node)).Should().BeFalse("another project's session has its own state");
        secondService.SetLocked(NodeReference.To(node), isLocked: true);
        await secondService.RestoreAsync(sceneId).ConfigureAwait(false);

        var reopened = kit.Create(first);
        await reopened.RestoreAsync(sceneId).ConfigureAwait(false);
        _ = reopened.IsHidden(NodeReference.To(node)).Should().BeTrue();
        _ = reopened.IsLocked(NodeReference.To(node)).Should().BeFalse("the other session's write does not reach this project");
        _ = kit.Settings.ContextsOf(SceneNodeInteraction.Key).Should().BeEquivalentTo([first.Context.SettingsScope, second.Context.SettingsScope]);
    }

    [TestMethod]
    public async Task MountChange_KeepsTheProjectsState()
    {
        using var session = OpenSession();
        var sceneId = SceneId.New();
        var node = NodeId.New();
        var kit = new SceneInteractionKit();
        var service = kit.Create(session);
        await service.RestoreAsync(sceneId).ConfigureAwait(false);
        var before = session.Context;

        // A mount change replaces the context with the same project id and root (scope topology, decision 7).
        session.ChangeContext(before with { CookedContentOrder = [] });
        service.SetHidden(NodeReference.To(node), isHidden: true);
        await service.RestoreAsync(sceneId).ConfigureAwait(false);

        _ = service.IsHidden(NodeReference.To(node)).Should().BeTrue();
        _ = kit.Settings.ContextsOf(SceneNodeInteraction.Key).Should().Equal([before.SettingsScope], "the project's settings scope is its root, which a mount change keeps");
    }

    [TestMethod]
    public async Task InvalidSavedState_PublishesFailureAndKeepsDefaults()
    {
        using var session = OpenSession();
        var kit = new SceneInteractionKit();
        kit.Settings.Store(
            SceneNodeInteraction.Key,
            session.Context.SettingsScope,
            new SceneNodeInteraction.ProjectInteraction(session.Context.ProjectId, null!));
        var service = kit.Create(session);

        await service.RestoreAsync(SceneId.New()).ConfigureAwait(false);

        _ = service.HiddenNodes().Should().BeEmpty();
        _ = service.Categories.Should().Be(SceneCategories.All);
        _ = kit.Published.SelectMany(result => result.Diagnostics).Should()
            .Contain(diagnostic => diagnostic.Code == DiagnosticCodes.SettingsPrefix + "INVALID_STATE");
    }

    [TestMethod]
    public async Task StaleRestoreAfterClear_DoesNotReapplyPriorState()
    {
        using var session = OpenSession();
        var sceneId = SceneId.New();
        var hidden = NodeId.New();
        var load = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var kit = new SceneInteractionKit();
        kit.Settings.BeforeLoad = () => load.Task;
        kit.Settings.Store(
            SceneNodeInteraction.Key,
            session.Context.SettingsScope,
            new SceneNodeInteraction.ProjectInteraction(
                session.Context.ProjectId,
                new Dictionary<SceneId, SceneNodeInteraction.SceneInteraction>
                {
                    [sceneId] = new([NodeReference.To(hidden)], [], new SceneCategories(ShowMeshes: false, ShowLights: false, ShowCameras: false)),
                }));
        var service = kit.Create(session);
        var restoration = service.RestoreAsync(sceneId);
        service.ClearActiveScene();
        load.SetResult();

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
        var kit = new SceneInteractionKit();
        var service = kit.Create(session);
        await service.RestoreAsync(sceneId).ConfigureAwait(false);

        service.SetLocked(door, isLocked: true);
        var restarted = kit.Create(session);
        await restarted.RestoreAsync(sceneId).ConfigureAwait(false);

        _ = restarted.IsLocked(door).Should().BeTrue("settings LLD 5.1: an instanced node is locked by its instance root and node path");
        _ = restarted.IsLocked(NodeReference.To(door.Node)).Should().BeFalse("locking an instanced node does not lock its instance root");
    }

    [TestMethod]
    public async Task GetLock_OfAnInstancedNode_IsOwnedByTheClosestLockedAncestorInItsInstance()
    {
        using var session = OpenSession();
        var world = CarWorld();
        var kit = new SceneInteractionKit();
        _ = kit.Structures.Publish(CarSource, world.Structure);
        var service = kit.Create(session);
        await service.RestoreAsync(world.Scene.Id).ConfigureAwait(false);

        _ = service.GetLock(world.Scene, world.Bolt).Should().Be(NodeLock.None);

        service.SetLocked(world.Wheel, isLocked: true);
        _ = service.GetLock(world.Scene, world.Bolt).Owner.Should().Be(world.Wheel, "a locked ancestor in the same instance protects what is below it, across nesting levels");
        _ = service.GetLock(world.Scene, world.Rim).Owner.Should().Be(world.Wheel);
        _ = service.GetLock(world.Scene, world.Wheel).Owner.Should().Be(world.Wheel, "a locked node is protected by itself");
        _ = service.GetLock(world.Scene, world.Spare).Should().Be(NodeLock.None, "a node outside the locked node's subtree is not protected by it");

        service.SetLocked(world.Rim, isLocked: true);
        _ = service.GetLock(world.Scene, world.Bolt).Owner.Should().Be(world.Rim, "the closest locked node owns the lock");
        _ = service.GetLock(world.Scene, world.Wheel).Owner.Should().Be(world.Wheel, "a lock below a node does not protect it");
    }

    [TestMethod]
    public async Task GetLock_OfAnInstancedNode_FallsBackToTheInstanceRootThenTheSceneAncestors()
    {
        using var session = OpenSession();
        var world = CarWorld();
        var kit = new SceneInteractionKit();
        _ = kit.Structures.Publish(CarSource, world.Structure);
        var service = kit.Create(session);
        await service.RestoreAsync(world.Scene.Id).ConfigureAwait(false);

        service.SetLocked(NodeReference.To(world.Group.Id), isLocked: true);
        _ = service.GetLock(world.Scene, world.Bolt).Owner.Should().Be(NodeReference.To(world.Group.Id), "an ancestor of the instance root locks what the instance brought in");

        service.SetLocked(NodeReference.To(world.Car.Id), isLocked: true);
        _ = service.GetLock(world.Scene, world.Bolt).Owner.Should().Be(NodeReference.To(world.Car.Id), "the instance root is closer than its ancestors");

        service.SetLocked(world.Wheel, isLocked: true);
        _ = service.GetLock(world.Scene, world.Bolt).Owner.Should().Be(world.Wheel, "a node of the instance is closer than the instance root");
        _ = service.GetLock(world.Scene, world.Spare).Owner.Should().Be(NodeReference.To(world.Car.Id));
    }

    [TestMethod]
    public async Task GetAncestors_ListsTheInstancesParentsThenTheInstanceRootThenItsSceneAncestors()
    {
        using var session = OpenSession();
        var world = CarWorld();
        var kit = new SceneInteractionKit();
        var service = kit.Create(session);

        _ = service.GetAncestors(world.Scene, world.Bolt).Should().BeNull("the instance's contents are not read");

        _ = kit.Structures.Publish(CarSource, world.Structure);

        _ = service.GetAncestors(world.Scene, world.Bolt).Should().Equal(
            world.Rim,
            world.Wheel,
            NodeReference.To(world.Car.Id),
            NodeReference.To(world.Group.Id));
        _ = service.GetAncestors(world.Scene, world.Spare).Should().Equal(NodeReference.To(world.Car.Id), NodeReference.To(world.Group.Id));
        _ = service.GetAncestors(world.Scene, NodeReference.To(world.Car.Id)).Should().Equal(NodeReference.To(world.Group.Id));
    }

    [TestMethod]
    public async Task GetLock_OfAnInstancedNode_WhoseInstanceIsNotReadIsProtectedByTheInstance()
    {
        using var session = OpenSession();
        var world = CarWorld();
        var kit = new SceneInteractionKit();
        var service = kit.Create(session);
        await service.RestoreAsync(world.Scene.Id).ConfigureAwait(false);

        var unread = service.GetLock(world.Scene, world.Bolt);

        _ = unread.Protects.Should().BeTrue("without the instance's contents nothing tells whether an ancestor is locked");
        _ = unread.UnreadInstance.Should().Be(world.Car.Id);
        _ = unread.Owner.Should().BeNull();

        _ = kit.Structures.Publish(CarSource, world.Structure.Where(node => !node.Path.Equals(world.Bolt.Path)));
        _ = service.GetLock(world.Scene, world.Bolt).UnreadInstance.Should().Be(world.Car.Id, "a node the read contents do not hold is still unread");

        _ = kit.Structures.Publish(CarSource, world.Structure);
        _ = service.GetLock(world.Scene, world.Bolt).Should().Be(NodeLock.None);
        _ = service.GetLock(world.Scene, NodeReference.To(world.Car.Id)).Should().Be(NodeLock.None, "a node of the scene needs no contents");
    }

    [TestMethod]
    public async Task GetLock_InAnInactiveScene_IsNone()
    {
        using var session = OpenSession();
        var world = CarWorld();
        var kit = new SceneInteractionKit();
        _ = kit.Structures.Publish(CarSource, world.Structure);
        var service = kit.Create(session);
        await service.RestoreAsync(world.Scene.Id).ConfigureAwait(false);
        service.SetLocked(NodeReference.To(world.Group.Id), isLocked: true);

        _ = service.GetLock(CreateScene(), world.Bolt).Should().Be(NodeLock.None, "an inactive scene must not inherit the active scene's lock state");
    }

    [TestMethod]
    public async Task LockedNodes_IsASnapshotOfTheActiveScenesLocks()
    {
        using var session = OpenSession();
        var sceneId = SceneId.New();
        var plain = NodeReference.To(NodeId.New());
        var instanced = new NodeReference(NodeId.New(), NodePath.Of(NodeId.New()));
        var hiddenOnly = NodeReference.To(NodeId.New());
        var service = new SceneInteractionKit().Create(session);
        await service.RestoreAsync(sceneId).ConfigureAwait(false);
        service.SetLocked(plain, isLocked: true);
        service.SetLocked(instanced, isLocked: true);
        service.SetHidden(hiddenOnly, isHidden: true);

        var snapshot = service.LockedNodes();
        service.SetLocked(plain, isLocked: false);

        _ = snapshot.Should().BeEquivalentTo([plain, instanced], "hidden nodes are not locked, and the snapshot does not follow later changes");
        _ = service.LockedNodes().Should().BeEquivalentTo([instanced]);

        await service.RestoreAsync(SceneId.New()).ConfigureAwait(false);
        _ = service.LockedNodes().Should().BeEmpty("another scene has its own locks");
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
        var kit = new SceneInteractionKit();
        var service = kit.Create(session);
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
        _ = StoredFor(kit, session).Scenes[sceneId].LockedNodeIds.Should().NotContain(gone);
        _ = StoredFor(kit, session).Scenes[sceneId].HiddenNodeIds.Should().NotContain(gone);

        service.DropUnresolvedNodes(root, _ => true);
        _ = changes.Should().Be(1, "nothing dropped, nothing changed");
    }

    [TestMethod]
    public async Task ClearActiveScene_ResetsCategoriesWithoutSavingEmptyScene()
    {
        using var session = OpenSession();
        var service = new SceneInteractionKit().Create(session);
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

    private static SceneNodeInteraction.ProjectInteraction StoredFor(SceneInteractionKit kit, TestSession session)
        => kit.Settings.Stored(SceneNodeInteraction.Key, session.Context.SettingsScope)
            ?? throw new InvalidOperationException("The service saved nothing for the project.");

    // A scene whose Group holds a Car, an instance of CarSource. The source brings in a Wheel with a Rim with a Bolt, and
    // a Spare beside the Wheel; the Structure lists them as the source's contents.
    private static CarScene CarWorld()
    {
        var scene = CreateScene();
        var group = new SceneNode(scene, "Group");
        var car = new SceneNode(scene, "Car");
        _ = car.AddComponent(new SceneInstanceComponent { Name = "Scene Instance", Source = CarSource });
        group.Hierarchy.AddChild(car);
        scene.RootNodes.Add(group);

        var wheel = NodePath.Of(NodeId.New());
        var rim = wheel.Append(NodeId.New());
        var bolt = rim.Append(NodeId.New());
        var spare = NodePath.Of(NodeId.New());
        return new CarScene(
            scene,
            group,
            car,
            new NodeReference(car.Id, wheel),
            new NodeReference(car.Id, rim),
            new NodeReference(car.Id, bolt),
            new NodeReference(car.Id, spare),
            [(wheel, "Wheel", null), (rim, "Rim", wheel), (bolt, "Bolt", rim), (spare, "Spare", null)]);
    }

    private sealed record CarScene(
        Scene Scene,
        SceneNode Group,
        SceneNode Car,
        NodeReference Wheel,
        NodeReference Rim,
        NodeReference Bolt,
        NodeReference Spare,
        IReadOnlyList<(NodePath Path, string Name, NodePath? Parent)> Structure);
}
