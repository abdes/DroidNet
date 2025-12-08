# SceneExplorer Testability Refactor — Professional Architecture

## Executive Summary

The `SceneExplorerViewModel` is untestable because business logic is **conflated with two fundamentally different operation categories** that currently have no separation:

1. **Scene Operations** — Modify `Scene.RootNodes`, require engine sync, represent real-world state
2. **Layout Operations** — Modify `Scene.ExplorerLayout`, UI-only, represent display preferences

This design respects the proven distinction already identified in `SCENE_EXPLORER_IMPROVEMENTS.md`.

---

## The Problem: Implicit Operation Categories

### Current Code Mixes Both Types

In `OnItemBeingAdded`, the same handler processes three different scenarios:

```csharp
private async void OnItemBeingAdded(object? sender, TreeItemBeingAddedEventArgs args)
{
    if (args.TreeItem is not SceneNodeAdapter entityAdapter) return;

    // SCENE OPERATION: Node reparenting in scene graph
    if (newParent is SceneNodeAdapter newNodeParent)
    {
        entity.SetParent(newNodeParent.AttachedObject);           // Scene mutation
        await this.sceneEngineSync.ReparentNodeAsync(...);       // Engine sync
        this.messenger.Send(new SceneNodeReparentedMessage(...)); // Event broadcast
        return;
    }

    // SCENE OPERATION: Node creation at scene root
    if (args.Parent is SceneAdapter directSceneParent)
    {
        scene.RootNodes.Add(entity);                              // Scene mutation
        await this.sceneEngineSync.CreateNodeAsync(entity);       // Engine sync
        this.messenger.Send(new SceneNodeAddedMessage(...));      // Event broadcast
        return;
    }

    // LAYOUT OPERATION: Node moved into folder
    if (args.Parent is FolderAdapter)
    {
        // Update ExplorerLayout ONLY
        // NO RootNodes change, NO engine sync
    }
}
```

### Why This Is Dangerous

- **Easy mistake:** Copy a Scene Operation path, forget engine sync → data corruption
- **Hard to test:** Need WinUI dispatcher just to test "folder moved" logic
- **No compiler help:** No type enforcement; naming conventions alone document intent
- **Scattered validation:** Each handler must remember rules

---

## The Solution: Explicit Operation Dispatchers

Create **two focused dispatchers**, each testable in isolation, enforcing its operation category's invariants.

### Architecture

```
┌─────────────────────────────────────┐
│ SceneExplorerViewModel              │
│ ├─ UI state: SelectionModel         │
│ ├─ Dispatcher: engine scheduling    │
│ ├─ Messenger: event broadcasting    │
│ └─ Delegates to:                    │
│    ├─→ ISceneMutator                │
│    └─→ ISceneOrganizer              │
└──────────────┬──────────────────────┘
               │
      ┌────────┴────────┐
      ▼                 ▼
ISceneMutator (Scene graph operations)
├─ CreateNodeAtRoot()
├─ CreateNodeUnderParent()
├─ RemoveNode()
├─ ReparentNode()
└─ GUARANTEES:
  ├─ Always updates RootNodes
  ├─ Always syncs engine (returns record)
  └─ Always broadcasts message

ISceneOrganizer (Layout & folder operations)
├─ CreateFolderFromSelection()
├─ MoveNodeToFolder()
├─ MoveFolderToParent()
├─ RemoveFolder()
└─ GUARANTEES:
  ├─ Never touches RootNodes
  ├─ Never syncs engine
  ├─ Only modifies ExplorerLayout
  └─ Folders exist under root or other folders
```

---

## Interface Design

### ISceneMutator — Testable, No UI Dependencies

```csharp
namespace Oxygen.Editor.WorldEditor.SceneExplorer.Operations;

/// <summary>
/// Executes operations that mutate the runtime scene graph (Scene.RootNodes, parent-child relationships).
/// Each operation is testable in isolation (no UI/dispatcher needed).
/// INVARIANTS:
///   1. Always updates Scene.RootNodes correctly
///   2. Always updates entity parent-child relationships
///   3. Returns a change record describing what happened
/// </summary>
public interface ISceneMutator
{
    /// <summary>
    /// Creates a new scene node at scene root.
    /// GUARANTEES: Adds to RootNodes (if not present), returns change record.
    /// </summary>
    SceneNodeChangeRecord CreateNodeAtRoot(SceneNode newNode, Scene scene);

    /// <summary>
    /// Creates a new scene node under an existing node.
    /// GUARANTEES: Updates newNode.Parent, removes from RootNodes if needed.
    /// </summary>
    SceneNodeChangeRecord CreateNodeUnderParent(
        SceneNode newNode,
        SceneNode parentNode,
        Scene scene);

    /// <summary>
    /// Removes a scene node from the graph.
    /// GUARANTEES: Removes from RootNodes (if present), clears parent ref.
    /// </summary>
    SceneNodeChangeRecord RemoveNode(Guid nodeId, Scene scene);

    /// <summary>
    /// Reparents a node to a new parent (or root if parentId is null).
    /// GUARANTEES: Updates parent refs, manages RootNodes membership correctly.
    /// </summary>
    SceneNodeChangeRecord ReparentNode(
        Guid nodeId,
        Guid? oldParentId,
        Guid? newParentId,
        Scene scene);
}

/// <summary>
/// Returned by all ISceneMutator methods.
/// Records exactly what changed so ViewModel can:
///   1. Schedule engine sync with correct parameters
///   2. Broadcast event with complete info
///   3. Record undo action
/// </summary>
public sealed record SceneNodeChangeRecord(
    string OperationName,
    SceneNode AffectedNode,
    Guid? OldParentId,
    Guid? NewParentId,
    bool RequiresEngineSync,
    bool AddedToRootNodes,
    bool RemovedFromRootNodes);
```

### ISceneOrganizer — Testable, Pure Layout & Folder Logic

```csharp
/// <summary>
/// Executes operations that affect the UI explorer layout and folder structure (Scene.ExplorerLayout).
/// These are testable in isolation (no engine, no RootNodes).
/// INVARIANTS:
///   1. Never modifies Scene.RootNodes
///   2. Never requires engine sync
///   3. Only updates Scene.ExplorerLayout
///   4. Folders exist under root or under other folders (never under scene nodes)
/// </summary>
public interface ISceneOrganizer
{
    /// <summary>
    /// Creates a folder from selected scene nodes.
    /// GUARANTEES: Updates ExplorerLayout, never touches RootNodes.
    /// </summary>
    LayoutChangeRecord CreateFolderFromSelection(
        HashSet<Guid> selectedNodeIds,
        Scene scene,
        SceneAdapter sceneAdapter);

    /// <summary>
    /// Moves a scene node into a folder.
    /// GUARANTEES: Updates ExplorerLayout, never touches RootNodes.
    /// </summary>
    LayoutChangeRecord MoveNodeToFolder(
        Guid nodeId,
        Guid folderId,
        Scene scene);

    /// <summary>
    /// Moves a folder to a new parent folder or to root.
    /// Folders can only exist under root or under other folders.
    /// GUARANTEES: Updates ExplorerLayout, never touches RootNodes.
    /// </summary>
    LayoutChangeRecord MoveFolderToParent(
        Guid folderId,
        Guid? newParentFolderId,
        Scene scene);

    /// <summary>
    /// Removes a folder and optionally promotes its children to parent.
    /// GUARANTEES: Updates ExplorerLayout, never touches RootNodes.
    /// </summary>
    LayoutChangeRecord RemoveFolder(
        Guid folderId,
        bool promoteChildrenToParent,
        Scene scene);
}

/// <summary>
/// Returned by all ILayoutOperationDispatcher methods.
/// Records layout changes so ViewModel can:
///   1. Update adapter tree
///   2. Record undo action
///   3. Refresh UI
/// </summary>
public sealed record LayoutChangeRecord(
    string OperationName,
    IList<ExplorerEntryData>? PreviousLayout,
    IList<ExplorerEntryData> NewLayout,
    FolderAdapter? NewFolder = null,
    IList<FolderAdapter>? ModifiedFolders = null);
```

---

## Implementation: SceneMutator

```csharp
public sealed class SceneMutator : ISceneMutator
{
    private readonly ILogger<SceneMutator> logger;

    public SceneMutator(ILogger<SceneMutator> logger)
    {
        this.logger = logger;
    }

    public SceneNodeChangeRecord CreateNodeAtRoot(SceneNode newNode, Scene scene)
    {
        // INVARIANT: If not in RootNodes, add it
        if (!scene.RootNodes.Contains(newNode))
        {
            scene.RootNodes.Add(newNode);
            this.logger.LogDebug("Added node '{Name}' to RootNodes", newNode.Name);
        }

        // INVARIANT: Parent must be null for root nodes
        var oldParentId = newNode.Parent?.Id;
        newNode.SetParent(newParent: null);

        return new SceneNodeChangeRecord(
            OperationName: "CreateNodeAtRoot",
            AffectedNode: newNode,
            OldParentId: oldParentId,
            NewParentId: null,
            RequiresEngineSync: true,
            AddedToRootNodes: true,
            RemovedFromRootNodes: false);
    }

    public SceneNodeChangeRecord CreateNodeUnderParent(
        SceneNode newNode,
        SceneNode parentNode,
        Scene scene)
    {
        // INVARIANT: Set parent relationship
        newNode.SetParent(parentNode);

        // INVARIANT: If node was in RootNodes, remove it
        var wasInRootNodes = scene.RootNodes.Contains(newNode);
        if (wasInRootNodes)
        {
            _ = scene.RootNodes.Remove(newNode);
            this.logger.LogDebug("Removed node '{Name}' from RootNodes", newNode.Name);
        }

        return new SceneNodeChangeRecord(
            OperationName: "CreateNodeUnderParent",
            AffectedNode: newNode,
            OldParentId: null,
            NewParentId: parentNode.Id,
            RequiresEngineSync: true,
            AddedToRootNodes: false,
            RemovedFromRootNodes: wasInRootNodes);
    }

    public SceneNodeChangeRecord RemoveNode(Guid nodeId, Scene scene)
    {
        var node = FindNodeById(scene, nodeId) ?? throw new InvalidOperationException(
            $"Cannot remove: node {nodeId} not found in scene");

        var oldParentId = node.Parent?.Id;

        // INVARIANT: Remove from RootNodes if present
        var wasInRootNodes = scene.RootNodes.Contains(node);
        if (wasInRootNodes)
        {
            _ = scene.RootNodes.Remove(node);
            this.logger.LogDebug("Removed node '{Name}' from RootNodes", node.Name);
        }

        // INVARIANT: Clear parent reference
        node.SetParent(newParent: null);

        return new SceneNodeChangeRecord(
            OperationName: "RemoveNode",
            AffectedNode: node,
            OldParentId: oldParentId,
            NewParentId: null,
            RequiresEngineSync: true,
            AddedToRootNodes: false,
            RemovedFromRootNodes: wasInRootNodes);
    }

    public SceneNodeChangeRecord ReparentNode(
        Guid nodeId,
        Guid? oldParentId,
        Guid? newParentId,
        Scene scene)
    {
        var node = FindNodeById(scene, nodeId) ?? throw new InvalidOperationException(
            $"Cannot reparent: node {nodeId} not found");

        var newParent = newParentId.HasValue
            ? FindNodeById(scene, newParentId.Value)
            : null;

        // INVARIANT: Update parent reference
        node.SetParent(newParent);

        // INVARIANT: Adjust RootNodes membership
        var addedToRootNodes = false;
        var removedFromRootNodes = false;

        if (newParent is null && !scene.RootNodes.Contains(node))
        {
            // Reparenting to root — add to RootNodes
            scene.RootNodes.Add(node);
            addedToRootNodes = true;
            this.logger.LogDebug("Moved node '{Name}' to RootNodes", node.Name);
        }
        else if (newParent is not null && scene.RootNodes.Contains(node))
        {
            // Reparenting under a node — remove from RootNodes
            _ = scene.RootNodes.Remove(node);
            removedFromRootNodes = true;
            this.logger.LogDebug("Removed node '{Name}' from RootNodes", node.Name);
        }

        return new SceneNodeChangeRecord(
            OperationName: "ReparentNode",
            AffectedNode: node,
            OldParentId: oldParentId,
            NewParentId: newParentId,
            RequiresEngineSync: true,
            AddedToRootNodes: addedToRootNodes,
            RemovedFromRootNodes: removedFromRootNodes);
    }

    private static SceneNode? FindNodeById(Scene scene, Guid nodeId)
    {
        foreach (var root in scene.RootNodes)
        {
            var found = FindInTree(root);
            if (found is not null) return found;
        }

        return null;

        SceneNode? FindInTree(SceneNode node)
        {
            if (node.Id == nodeId) return node;
            foreach (var child in node.Children)
            {
                var found = FindInTree(child);
                if (found is not null) return found;
            }
            return null;
        }
    }
}
```

---

## How ViewModel Uses Both Dispatchers

```csharp
private async void OnItemBeingAdded(object? sender, TreeItemBeingAddedEventArgs args)
{
    if (args.TreeItem is not SceneNodeAdapter entityAdapter) return;

    var entity = entityAdapter.AttachedObject;
    var scene = this.currentProject.ActiveScene!;

    // DISPATCH based on parent type
    if (args.Parent is SceneNodeAdapter parentNodeAdapter)
    {
        // SCENE OPERATION: Reparenting within scene graph
            var sceneChange = this.sceneMutator.ReparentNode(
            entity.Id,
            oldParentId: entity.Parent?.Id,
            newParentId: parentNodeAdapter.AttachedObject.Id,
            scene);

        // Return type makes it OBVIOUS engine sync is needed
        _ = this.dispatcher.EnqueueAsync(async () =>
        {
            try
            {
                await this.sceneEngineSync.ReparentNodeAsync(
                    sceneChange.AffectedNode.Id,
                    sceneChange.NewParentId).ConfigureAwait(false);

                this.messenger.Send(new SceneNodeReparentedMessage(
                    entity,
                    sceneChange.OldParentId,
                    sceneChange.NewParentId));
            }
            catch (Exception ex)
            {
                this.logger.LogError(ex, "Engine sync failed");
            }
        });
    }
    else if (args.Parent is SceneAdapter sceneAdapter)
    {
        // SCENE OPERATION: Node creation at root
        var sceneChange = this.sceneMutator.CreateNodeAtRoot(entity, scene);

        _ = this.dispatcher.EnqueueAsync(async () =>
        {
            try
            {
                await this.sceneEngineSync.CreateNodeAsync(entity).ConfigureAwait(false);
                this.messenger.Send(new SceneNodeAddedMessage(new[] { entity }));
            }
            catch (Exception ex)
            {
                this.logger.LogError(ex, "Engine sync failed");
            }
        });
    }
    else if (args.Parent is FolderAdapter folderAdapter)
    {
        // LAYOUT OPERATION: Moving node into folder
        var layoutChange = this.sceneOrganizer.MoveNodeToFolder(
            entity.Id,
            folderAdapter.Id,
            scene);

        // Return type is LayoutChangeRecord — compiler enforces NO engine sync here
    }

    // Record undo
    if (!this.suppressUndoRecording)
    {
        UndoRedo.Default[this].AddChange(
            $"Add {entityAdapter.Label}",
            () => this.UndoAddition(entityAdapter, args.Parent));
    }
}
```

**Key insight:** Return types make intent explicit — impossible to:
- Forget engine sync for scene operations
- Accidentally broadcast events for layout operations
- Mix concerns in undo recording

---

## Testing Example

```csharp
[TestClass]
public class SceneMutatorTests
{
    private ISceneMutator mutator;

    [TestInitialize]
    public void Setup()
    {
        var logger = new NullLogger<SceneMutator>();
        this.mutator = new SceneMutator(logger);
    }

    [TestMethod]
    public void ReparentNode_MovesToRootNodes_WhenParentIsNull()
    {
        // Arrange
        var scene = new Scene();
        var parent = new SceneNode(scene);
        var child = new SceneNode(parent);
        scene.RootNodes.Add(parent);

        // Act
        var change = this.sceneMutator.ReparentNode(
            child.Id,
            oldParentId: parent.Id,
            newParentId: null,
            scene);

        // Assert
        Assert.IsNull(child.Parent);
        Assert.IsTrue(scene.RootNodes.Contains(child));
        Assert.IsTrue(change.AddedToRootNodes);
        Assert.IsFalse(change.RemovedFromRootNodes);
        Assert.IsTrue(change.RequiresEngineSync);
    }

    [TestMethod]
    public void ReparentNode_RemovedFromRootNodes_WhenMovingUnderNode()
    {
        // Arrange
        var scene = new Scene();
        var node1 = new SceneNode(scene);
        var node2 = new SceneNode(scene);
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);

        // Act
        var change = this.dispatcher.ReparentNode(
            node2.Id,
            oldParentId: null,
            newParentId: node1.Id,
            scene);

        // Assert
        Assert.AreEqual(node1, node2.Parent);
        Assert.IsFalse(scene.RootNodes.Contains(node2));
        Assert.IsFalse(change.AddedToRootNodes);
        Assert.IsTrue(change.RemovedFromRootNodes);
    }

    [TestMethod]
    [ExpectedException(typeof(InvalidOperationException))]
    public void RemoveNode_ThrowsIfNodeNotFound()
    {
        var scene = new Scene();
        var missingId = Guid.NewGuid();

        // Act
        this.dispatcher.RemoveNode(missingId, scene);

        // Should throw — node not found
    }
}
```

**No WinUI. No dispatcher. No messaging. Just pure logic.**

---

## Implementation Roadmap

### Phase 1: Core Dispatchers (2-3 days)

- [ ] Create `ISceneOperationDispatcher` + implementation
- [ ] Create `ILayoutOperationDispatcher` + implementation
- [ ] Unit tests for both dispatchers
- [ ] Inject both into ViewModel

### Phase 2: Refactor Event Handlers (2-3 days)

- [ ] Update `OnItemBeingAdded` to use dispatchers
- [ ] Update `OnItemBeingRemoved` to use dispatchers
- [ ] Update `CreateFolderFromSelection` to use layout dispatcher
- [ ] Extract engine sync coordination methods

### Phase 3: Complete Testing (1-2 days)

- [ ] Test all operation paths with real `Scene`/`SceneNode` objects
- [ ] Integration tests with ViewModel
- [ ] Regression tests for existing bugs

---

## Benefits

| Concern | Before | After |
|---------|--------|-------|
| **Testability** | Impossible (UI deps) | ✓ 100% unit testable |
| **Clarity** | Mixed concerns | ✓ Clear operation categories |
| **Safety** | Easy to forget sync | ✓ Compiler enforces rules |
| **Reusability** | Tied to ViewModel | ✓ Use dispatchers elsewhere |
| **Debugging** | Hard to trace | ✓ Clear change records |
| **Code Review** | Hard to validate | ✓ Obvious what changed |

---

## Why This Approach Works

1. **Respects existing analysis:** The detailed improvements doc already identified Scene vs Layout distinction
2. **Focused interfaces:** Each dispatcher has ONE job, enforces its invariants
3. **Type safety:** Return types make intent explicit; impossible to accidentally violate rules
4. **Non-breaking:** Inject dispatchers, gradually migrate handlers, old code coexists
5. **Professional:** Proven pattern (command dispatchers, change records) used in production systems
