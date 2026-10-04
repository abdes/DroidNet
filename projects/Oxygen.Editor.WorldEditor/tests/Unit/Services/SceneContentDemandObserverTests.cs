// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.TimeMachine;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneExplorer.Operations;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Services;

/// <summary>
/// Guards the model-notification contract consumed by the <see cref="SceneContentDemandService"/>
/// reference observers: every command-driven structural mutation must raise change notifications on
/// the collections and reference properties its observation walk subscribes, and the walk over the
/// mutated model must expose the new reference graph so a rescan re-subscribes it and drops the
/// obsolete sources. The service-level behavior runs on the UI host (see the Unit.UI content demand
/// tests); this suite proves the command side reaches the observers at all.
/// </summary>
[TestClass]
[TestCategory("Scene Content Demand")]
public sealed class SceneContentDemandObserverTests
{
    private static readonly ProjectInfo ObserverTestProjectInfo = new("Demand observer tests", Category.Games, Path.Combine(Path.GetTempPath(), "Oxygen-Demand-Observer-Tests"));

    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task CreateNodeAsync_AtRoot_ReachesRootNodesObserverAndExposesNewNodeCollections()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var context = CreateContext(scene);
        var recorder = new NotificationRecorder();
        recorder.Rescan(scene);
        recorder.Clear();

        var result = await fixture.Sut.CreateNodeAsync(context, parentNodeId: null, parentFolderId: null, "New Node").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = recorder.CollectionEvents.Contains(scene.RootNodes).Should().BeTrue("the demand service learns about created root nodes through RootNodes");
        recorder.Rescan(scene);
        var sources = ReferenceSources(scene).ToList();
        _ = sources.Contains(result.Value!.Children).Should().BeTrue("a rescan after the command re-subscribes the created node's children");
        _ = sources.Contains(result.Value!.Components).Should().BeTrue("a rescan after the command re-subscribes the created node's components");
    }

    [TestMethod]
    public async Task CreateNodeAsync_UnderParent_ReachesParentChildrenObserver()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent" };
        scene.RootNodes.Add(parent);
        var context = CreateContext(scene);
        var recorder = new NotificationRecorder();
        recorder.Rescan(scene);
        recorder.Clear();

        var result = await fixture.Sut.CreateNodeAsync(context, parentNodeId: parent.Id, parentFolderId: null, "Child").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = recorder.CollectionEvents.Contains(parent.Children).Should().BeTrue("the demand service learns about created child nodes through the parent's Children");
        _ = recorder.CollectionEvents.Contains(scene.RootNodes).Should().BeFalse("parented creation must not go through the root collection");
        recorder.Rescan(scene);
        var sources = ReferenceSources(scene).ToList();
        _ = sources.Contains(result.Value!.Children).Should().BeTrue("a rescan after the command re-subscribes the created node's children");
        _ = sources.Contains(result.Value!.Components).Should().BeTrue("a rescan after the command re-subscribes the created node's components");
    }

    [TestMethod]
    public async Task AddComponentAsync_ReachesComponentsObserverAndExposesGeometryReferenceSources()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);
        var recorder = new NotificationRecorder();
        recorder.Rescan(scene);
        recorder.Clear();

        var result = await fixture.Sut.AddComponentAsync(context, node.Id, typeof(GeometryComponent)).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        var geometry = result.Value!.Should().BeOfType<GeometryComponent>().Subject;
        _ = recorder.CollectionEvents.Contains(node.Components).Should().BeTrue("the demand service learns about added components through the node's Components");
        recorder.Rescan(scene);
        var sources = ReferenceSources(scene).ToList();
        _ = sources.Contains(geometry).Should().BeTrue("a rescan after the command re-subscribes the new geometry's reference property");
        _ = sources.Contains(geometry.OverrideSlots).Should().BeTrue("a rescan after the command re-subscribes the new geometry's override slots");
    }

    [TestMethod]
    public async Task RemoveComponentAsync_ReachesComponentsObserverAndDropsGeometryReferenceSources()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);
        var added = await fixture.Sut.AddComponentAsync(context, node.Id, typeof(GeometryComponent)).ConfigureAwait(false);
        var recorder = new NotificationRecorder();
        recorder.Rescan(scene);
        recorder.Clear();

        var result = await fixture.Sut.RemoveComponentAsync(context, node.Id, added.Value!.Id).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = recorder.CollectionEvents.Contains(node.Components).Should().BeTrue("the demand service learns about removed components through the node's Components");
        recorder.Rescan(scene);
        _ = ReferenceSources(scene).ToList().Contains(added.Value).Should().BeFalse("a rescan after the command drops the removed geometry and its obsolete reference");
    }

    [TestMethod]
    public async Task ReparentNodesAsync_ReachesSourceAndDestinationObserversAndKeepsReferencesInWalk()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Node" };
        var geometry = new GeometryComponent { Name = "Geometry" };
        _ = node.AddComponent(geometry);
        var destination = new SceneNode(scene) { Name = "Destination" };
        scene.RootNodes.Add(node);
        scene.RootNodes.Add(destination);
        var context = CreateContext(scene);
        var recorder = new NotificationRecorder();
        recorder.Rescan(scene);
        recorder.Clear();

        var result = await fixture.Sut.ReparentNodesAsync(context, [node.Id], destination.Id, preserveWorldTransform: false).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = node.Parent.Should().BeSameAs(destination);
        _ = recorder.CollectionEvents.Contains(scene.RootNodes).Should().BeTrue("a reparent notifies the collection the node left");
        _ = recorder.CollectionEvents.Contains(destination.Children).Should().BeTrue("a reparent notifies the collection the node entered");
        recorder.Rescan(scene);
        var sources = ReferenceSources(scene).ToList();
        _ = sources.Contains(node.Children).Should().BeTrue("a reparented node stays in the observed graph");
        _ = sources.Contains(node.Components).Should().BeTrue("a reparented node stays in the observed graph");
        _ = sources.Contains(geometry).Should().BeTrue("a reparented node keeps its references in the observed graph, so its demands stay current");
    }

    [TestMethod]
    public async Task DeleteNodesAsync_ReachesRootNodesObserverAndDropsSubtreeFromWalk()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var root = new SceneNode(scene) { Name = "Root" };
        var child = new SceneNode(scene) { Name = "Child" };
        var geometry = new GeometryComponent { Name = "Geometry" };
        _ = child.AddComponent(geometry);
        root.AddChild(child);
        scene.RootNodes.Add(root);
        var context = CreateContext(scene);
        var recorder = new NotificationRecorder();
        recorder.Rescan(scene);
        recorder.Clear();

        var result = await fixture.Sut.DeleteNodesAsync(context, [root.Id]).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = recorder.CollectionEvents.Contains(scene.RootNodes).Should().BeTrue("a subtree deletion notifies through the observed collection that held the subtree root");
        recorder.Rescan(scene);
        var sources = ReferenceSources(scene).ToList();
        _ = sources.Contains(root.Children).Should().BeFalse("a rescan after the command drops the deleted subtree root");
        _ = sources.Contains(child.Children).Should().BeFalse("a rescan after the command drops the deleted subtree children");
        _ = sources.Contains(child.Components).Should().BeFalse("a rescan after the command drops the deleted subtree components");
        _ = sources.Contains(geometry).Should().BeFalse("a rescan after the command drops the deleted subtree's obsolete geometry reference");
    }

    [TestMethod]
    public async Task DeleteNodesAsync_Undo_ReachesRootNodesObserverAndRestoresWalk()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var root = new SceneNode(scene) { Name = "Root" };
        var child = new SceneNode(scene) { Name = "Child" };
        root.AddChild(child);
        scene.RootNodes.Add(root);
        var context = CreateContext(scene);
        _ = (await fixture.Sut.DeleteNodesAsync(context, [root.Id]).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        var recorder = new NotificationRecorder();
        recorder.Rescan(scene);
        recorder.Clear();

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = scene.RootNodes.Should().Contain(root);
        _ = recorder.CollectionEvents.Contains(scene.RootNodes).Should().BeTrue("undo of a deletion re-inserts through the observed collection");
        recorder.Rescan(scene);
        var sources = ReferenceSources(scene).ToList();
        _ = sources.Contains(root.Children).Should().BeTrue("a rescan after the undo restores the subtree root into the observed graph");
        _ = sources.Contains(child.Children).Should().BeTrue("a rescan after the undo restores the subtree children into the observed graph");
        _ = sources.Contains(child.Components).Should().BeTrue("a rescan after the undo restores the subtree components into the observed graph");
    }

    [TestMethod]
    public async Task RenameNodeAsync_RaisesNoReferenceNotificationAndKeepsWalkStable()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Node" };
        var geometry = new GeometryComponent { Name = "Geometry" };
        _ = node.AddComponent(geometry);
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);
        var recorder = new NotificationRecorder();
        recorder.Rescan(scene);
        recorder.Clear();

        var result = await fixture.Sut.RenameNodeAsync(context, node.Id, "Renamed").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = node.Name.Should().Be("Renamed");
        _ = recorder.CollectionEvents.Should().BeEmpty("a rename touches no observed collection");
        _ = recorder.PropertyEvents.Should().BeEmpty("a rename is not a reference change on a geometry component or material slot");
        var sources = ReferenceSources(scene).ToList();
        _ = sources.Contains(node.Children).Should().BeTrue("the observed graph keeps the renamed node");
        _ = sources.Contains(node.Components).Should().BeTrue("the observed graph keeps the renamed node's components");
        _ = sources.Contains(geometry).Should().BeTrue("the renamed node's geometry reference survives, so no demand turns stale and none is retired spuriously");
    }

    /// <summary>
    /// Enumerates every notification source the demand service's reference walk subscribes, mirroring
    /// its observation of <see cref="Scene.RootNodes"/>, per-node <c>Children</c>/<c>Components</c> and
    /// per-geometry property/override slots. Keeping this walk in the test pins the model-side contract:
    /// if a command mutation stops reaching one of these sources, the demand refresh silently breaks.
    /// </summary>
    /// <param name="scene">The scene to walk.</param>
    /// <returns>The collections and reference properties a rescan observes.</returns>
    private static IEnumerable<object> ReferenceSources(Scene scene)
    {
        yield return scene.RootNodes;
        foreach (var node in scene.AllNodes)
        {
            yield return node.Children;
            yield return node.Components;
            foreach (var geometry in node.Components.OfType<GeometryComponent>())
            {
                yield return geometry;
                yield return geometry.OverrideSlots;
                foreach (var slot in geometry.OverrideSlots.OfType<MaterialsSlot>())
                {
                    yield return slot;
                }
            }
        }
    }

    private static Scene CreateScene()
    {
        var project = new Mock<IProject>();
        _ = project.SetupGet(value => value.ProjectInfo).Returns(ObserverTestProjectInfo);
        return new Scene(project.Object) { Name = "Observer Scene" };
    }

    private static SceneDocumentCommandContext CreateContext(Scene scene)
    {
        var metadata = new SceneDocumentMetadata { Title = scene.Name };
        return new(metadata.DocumentId, metadata, scene, new HistoryKeeper(scene));
    }

    private static Fixture CreateFixture()
    {
        var sync = new Mock<ISceneEngineSync>();
        _ = sync.Setup(value => value.CreateNodeAsync(It.IsAny<SceneNode>(), It.IsAny<Guid?>())).Returns(Task.CompletedTask);
        _ = sync.Setup(value => value.RemoveNodeHierarchiesAsync(It.IsAny<Scene>(), It.IsAny<IReadOnlyList<Guid>>())).Returns(Task.CompletedTask);
        _ = sync.Setup(value => value.ReparentNodeAsync(It.IsAny<Scene>(), It.IsAny<Guid>(), It.IsAny<Guid?>(), It.IsAny<bool>())).Returns(Task.CompletedTask);
        _ = sync.Setup(value => value.ReparentHierarchiesAsync(It.IsAny<Scene>(), It.IsAny<IReadOnlyList<Guid>>(), It.IsAny<Guid?>(), It.IsAny<bool>())).Returns(Task.CompletedTask);
        _ = sync.Setup(value => value.RenameNodeAsync(It.IsAny<Scene>(), It.IsAny<Guid>(), It.IsAny<string>()))
            .ReturnsAsync(new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.NodeRename, AffectedScope.Empty));
        _ = sync.Setup(value => value.UpdateNodeTransformAsync(It.IsAny<Scene>(), It.IsAny<SceneNode>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditTransform, AffectedScope.Empty));
        _ = sync.Setup(value => value.AttachGeometryAsync(It.IsAny<Scene>(), It.IsAny<SceneNode>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.AddComponent, AffectedScope.Empty));
        _ = sync.Setup(value => value.DetachGeometryAsync(It.IsAny<Scene>(), It.IsAny<Guid>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.RemoveComponent, AffectedScope.Empty));
        var documents = new Mock<IDocumentService>();
        _ = documents.Setup(value => value.UpdateMetadataAsync(It.IsAny<WindowId>(), It.IsAny<Guid>(), It.IsAny<IDocumentMetadata>())).ReturnsAsync(value: true);
        var projects = new ProjectContextService();
        projects.Activate(ProjectContext.FromProjectInfo(ObserverTestProjectInfo));
        var sut = new SceneDocumentCommandService(
            Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(),
            new SceneSelectionService(),
            sync.Object,
            Mock.Of<IProjectManagerService>(),
            documents.Object,
            default,
            new StrongReferenceMessenger(),
            Mock.Of<IOperationResultPublisher>(),
            new OperationStatusReducer(),
            Mock.Of<IGeometryMaterialSlotProvider>(),
            projects,
            new SceneMutator(NullLogger<SceneMutator>.Instance),
            new SceneOrganizer(NullLogger<SceneOrganizer>.Instance));
        return new(sut, sync);
    }

    private sealed record Fixture(SceneDocumentCommandService Sut, Mock<ISceneEngineSync> Sync);

    /// <summary>Records which observed sources raised notifications during a command.</summary>
    private sealed class NotificationRecorder
    {
        private readonly HashSet<object> subscribed = [];

        public List<object> CollectionEvents { get; } = [];

        public List<object> PropertyEvents { get; } = [];

        public void Rescan(Scene scene)
        {
            foreach (var source in ReferenceSources(scene))
            {
                if (!this.subscribed.Add(source))
                {
                    continue;
                }

                if (source is INotifyCollectionChanged collection)
                {
                    collection.CollectionChanged += (sender, _) => this.CollectionEvents.Add(sender!);
                }

                if (source is INotifyPropertyChanged observable)
                {
                    observable.PropertyChanged += (sender, _) => this.PropertyEvents.Add(sender!);
                }
            }
        }

        public void Clear()
        {
            this.CollectionEvents.Clear();
            this.PropertyEvents.Clear();
        }
    }
}
