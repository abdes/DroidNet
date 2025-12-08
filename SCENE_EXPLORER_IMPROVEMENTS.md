# Scene Explorer: Design Improvement Recommendations

## Executive Summary

After deep analysis, I've identified three critical areas for improvement:

1. **Clarity & Explicitness**: The UI/Scene hierarchy distinction is implicit and scattered; explicit separation would make code intent clear
2. **Undo/Redo Reliability**: Multiple issues with state consistency, tree collapse on undo, and layout reconstruction
3. **UX Issues**: Tree control jumps, folder collapses, nested folder creation bugs, expansion state loss

This document provides concrete, implementable recommendations with code examples.

---

## Part 1: Improving Clarity About UI vs Scene Distinction

### Current Problem

The code blurs the distinction between:
- **Scene Operations** (affect `Scene.RootNodes`, require engine sync)
- **Layout Operations** (affect `Scene.ExplorerLayout`, UI-only)

Evidence:
```csharp
// In OnItemBeingAdded, code path differs by parent type:
if (newParent is SceneNodeAdapter)        // Mutation: SetParent + ReparentNodeAsync
if (args.Parent is SceneAdapter)          // Mutation: add to RootNodes + CreateNodeAsync
if (args.Parent is FolderAdapter)         // Layout-only: update ExplorerLayout
```

This mixing makes it **easy to accidentally treat layout-only operations as scene mutations**.

### Recommendation 1.1: Introduce Semantic Operation Types

Create explicit types that declare intent:

```csharp
namespace Oxygen.Editor.WorldEditor.SceneExplorer.Operations;

/// <summary>
/// Base for all tree operations. Distinguishes between:
/// - SceneOperation: affects scene graph (parent-child relationships, engine sync)
/// - LayoutOperation: affects explorer layout (folders, display order)
/// </summary>
public abstract record SceneExplorerOperation;

/// <summary>
/// Marks operations that mutate the runtime scene graph.
/// These MUST synchronize with the engine and update Scene.RootNodes.
/// </summary>
public abstract record SceneOperation : SceneExplorerOperation;

/// <summary>
/// Creates a new scene node. Always syncs engine.
/// </summary>
public sealed record CreateSceneNodeOperation(SceneNode Node, Guid? ParentNodeId = null)
    : SceneOperation;

/// <summary>
/// Removes a scene node. Always syncs engine.
/// </summary>
public sealed record RemoveSceneNodeOperation(Guid NodeId)
    : SceneOperation;

/// <summary>
/// Changes parent-child relationship in scene graph. Always syncs engine.
/// </summary>
public sealed record ReparentSceneNodeOperation(Guid NodeId, Guid? OldParentId, Guid? NewParentId)
    : SceneOperation;

/// <summary>
/// Marks operations that affect only the UI explorer layout.
/// These NEVER sync engine or modify Scene.RootNodes.
/// </summary>
public abstract record LayoutOperation : SceneExplorerOperation;

/// <summary>
/// Organizes nodes under a folder (UI-only).
/// Does NOT affect scene.RootNodes or engine.
/// </summary>
public sealed record CreateFolderOperation(
    Guid FolderId,
    string FolderName,
    IList<ExplorerEntryData> MovedEntries)
    : LayoutOperation;

/// <summary>
/// Removes a folder (UI-only).
/// Does NOT affect scene.RootNodes or engine.
/// </summary>
public sealed record RemoveFolderOperation(Guid FolderId)
    : LayoutOperation;
```

**Benefits**:
- Compiler enforces intent (can't accidentally mix scene + layout ops)
- Code review becomes easier ("this is a layout op, no engine sync needed")
- Logging/debugging shows operation type explicitly

### Recommendation 1.2: Create Semantic Operation Dispatchers

Add a dispatcher that enforces the scene/layout distinction:

```csharp
/// <summary>
/// Executes scene and layout operations with guaranteed safety properties:
/// - SceneOperations always sync engine and update RootNodes
/// - LayoutOperations never sync engine or change RootNodes
/// </summary>
public sealed class SceneExplorerOperationDispatcher
{
    private readonly ISceneEngineSync sceneEngineSync;
    private readonly IMessenger messenger;
    private readonly ILogger<SceneExplorerOperationDispatcher> logger;

    public async Task ExecuteAsync(SceneOperation operation, Scene scene)
    {
        try
        {
            switch (operation)
            {
                case CreateSceneNodeOperation create:
                    // MUST: Add to RootNodes if needed
                    if (!scene.RootNodes.Contains(create.Node))
                        scene.RootNodes.Add(create.Node);

                    // MUST: Sync engine
                    await this.sceneEngineSync.CreateNodeAsync(create.Node, create.ParentNodeId)
                        .ConfigureAwait(false);

                    // MUST: Broadcast message
                    this.messenger.Send(new SceneNodeAddedMessage(new[] { create.Node }));
                    break;

                case RemoveSceneNodeOperation remove:
                    // MUST: Remove from RootNodes if present
                    _ = scene.RootNodes.Remove(/* find node by id */);

                    // MUST: Sync engine
                    await this.sceneEngineSync.RemoveNodeAsync(remove.NodeId)
                        .ConfigureAwait(false);

                    // MUST: Broadcast message
                    this.messenger.Send(new SceneNodeRemovedMessage(...));
                    break;

                // ... other SceneOperation cases
            }
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "Failed to execute scene operation: {Op}", operation);
            throw;
        }
    }

    public void Execute(LayoutOperation operation, Scene scene)
    {
        try
        {
            switch (operation)
            {
                case CreateFolderOperation create:
                    // NO engine sync
                    // NO RootNodes modification
                    // ONLY: Update ExplorerLayout
                    scene.ExplorerLayout ??= new List<ExplorerEntryData>();
                    scene.ExplorerLayout.Insert(0, new ExplorerEntryData
                    {
                        Type = "Folder",
                        FolderId = create.FolderId,
                        Name = create.FolderName,
                        Children = create.MovedEntries
                    });
                    break;

                // ... other LayoutOperation cases
            }
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "Failed to execute layout operation: {Op}", operation);
            throw;
        }
    }
}
```

**Benefits**:
- Single source of truth for scene mutations
- Impossible to forget engine sync
- All paths log consistently
- Easier to add metrics, validation, etc.

### Recommendation 1.3: Rename Key Variables for Clarity

Make the scene/layout distinction obvious in variable names:

**Current (Ambiguous)**:
```csharp
private Dictionary<SceneNodeAdapter, System.Guid> capturedOldParent;
private HashSet<SceneNodeAdapter> deletedAdapters;
```

**Improved (Explicit)**:
```csharp
// Captures scene-parent for detecting scene-graph reparents during moves
private Dictionary<SceneNodeAdapter, System.Guid> capturedSceneParentDuringMove;

// Tracks adapters deleted from scene (not moved between folders)
private HashSet<SceneNodeAdapter> adaptersPendingEngineRemoval;
```

### Recommendation 1.4: Separate Event Handlers by Operation Type

Currently, single handlers mix scene and layout logic:

```csharp
// CURRENT: Mixed logic, hard to follow
private async void OnItemBeingAdded(TreeItemBeingAddedEventArgs args)
{
    if (args.TreeItem is not SceneNodeAdapter entity)
        return;

    if (newParent is SceneNodeAdapter nodeParent)       // Scene operation
    { /* reparent */ }
    else if (args.Parent is SceneAdapter)               // Scene operation
    { /* create */ }
    else if (args.Parent is FolderAdapter)              // Layout operation
    { /* folder move */ }
}
```

**Improved approach**: Separate into distinct paths:

```csharp
private async void OnItemBeingAdded(TreeItemBeingAddedEventArgs args)
{
    if (args.TreeItem is not SceneNodeAdapter entityAdapter)
        return;

    // Dispatch to appropriate handler based on parent type
    switch (args.Parent)
    {
        case SceneNodeAdapter nodeParent:
            await this.OnSceneNodeBeingReparentedToNode(entityAdapter, nodeParent)
                .ConfigureAwait(false);
            break;

        case SceneAdapter sceneParent:
            await this.OnSceneNodeBeingAddedToScene(entityAdapter, sceneParent)
                .ConfigureAwait(false);
            break;

        case FolderAdapter folder:
            await this.OnSceneNodeBeingMovedToFolder(entityAdapter, folder)
                .ConfigureAwait(false);
            break;
    }
}

/// <summary>
/// Handles scene-graph reparent: node moved from one SceneNode parent to another.
/// REQUIRES: engine sync, model mutation.
/// </summary>
private async Task OnSceneNodeBeingReparentedToNode(SceneNodeAdapter adapter, SceneNodeAdapter newParent)
{
    var entity = adapter.AttachedObject;
    var newParentNode = newParent.AttachedObject;

    // MODEL MUTATION
    entity.SetParent(newParentNode);

    // ENGINE SYNC
    if (entity.IsActive)
    {
        await this.sceneEngineSync.ReparentNodeAsync(entity.Id, newParentNode.Id)
            .ConfigureAwait(false);
    }

    // MESSAGING
    this.messenger.Send(new SceneNodeReparentedMessage(entity, ...));
}

/// <summary>
/// Handles scene creation: new node added to scene root.
/// REQUIRES: engine sync, model mutation.
/// </summary>
private async Task OnSceneNodeBeingAddedToScene(SceneNodeAdapter adapter, SceneAdapter scene)
{
    var entity = adapter.AttachedObject;

    // MODEL MUTATION
    if (!scene.AttachedObject.RootNodes.Contains(entity))
        scene.AttachedObject.RootNodes.Add(entity);

    // UPDATE LAYOUT
    this.UpdateExplorerLayoutForNewNode(entity, scene.AttachedObject);

    // ENGINE SYNC
    await this.sceneEngineSync.CreateNodeAsync(entity).ConfigureAwait(false);

    // MESSAGING
    this.messenger.Send(new SceneNodeAddedMessage(new[] { entity }));
}

/// <summary>
/// Handles layout move: node moved between folders (UI-only).
/// DOES NOT: modify scene.RootNodes, sync engine.
/// </summary>
private async Task OnSceneNodeBeingMovedToFolder(SceneNodeAdapter adapter, FolderAdapter folder)
{
    var entity = adapter.AttachedObject;

    // UPDATE LAYOUT ONLY
    this.UpdateExplorerLayoutForFolderMove(entity, folder);

    // Ensure node exists in engine (if not created yet)
    if (!entity.IsActive)
    {
        await this.sceneEngineSync.CreateNodeAsync(entity).ConfigureAwait(false);
    }
}
```

**Benefits**:
- Clear separation: each method has single responsibility
- Easy to add logging, validation per operation type
- Compile-time guarantee: can't mix scene + layout logic in one method

---

## Part 2: Improving Undo/Redo Reliability

### Current Problems

1. **ReloadChildrenAsync loses state**: Calling `ReloadChildrenAsync()` on undo completely recreates all adapters, losing:
   - Expand/collapse state
   - Scroll position
   - Internal adapter state

2. **Double tree reconstruction**: On undo of folder creation:
   - Restores `scene.ExplorerLayout`
   - Calls `ReloadChildrenAsync()` (reconstructs all adapters)
   - Calls `InitializeRootAsync()` (redundant, reconstructs again!)

3. **Unsafe undo actions**: Undo closures can fail silently:
   ```csharp
   UndoRedo.Default[this].AddChange("RemoveItem(...)", () => {
       try {
           RemoveItemAsync(item).GetAwaiter().GetResult();
       } catch (Exception ex) {
           // Swallowed! User never knows undo failed
       }
   });
   ```

4. **No state validation**: Layout can become inconsistent after undo if:
   - Layout references deleted nodes
   - Layout nesting is invalid
   - Node IDs don't exist

### Recommendation 2.1: Preserve Adapter State Instead of Rebuilding

Instead of destroying and recreating adapters, mutate the layout in-place:

```csharp
/// <summary>
/// Reloads layout WITHOUT recreating adapters, preserving expand/collapse state.
/// Performs in-place mutations on existing adapters when possible.
/// </summary>
public async Task RefreshLayoutAsync()
{
    // If ExplorerLayout is null or empty, rebuild from RootNodes
    var layout = this.AttachedObject.ExplorerLayout;
    if (layout is null || layout.Count == 0)
    {
        await this.ReloadChildrenAsync().ConfigureAwait(false);
        return;
    }

    // Otherwise, mutate the existing tree to match layout
    // This preserves expand/collapse state
    await this.ReconcileAdaptersWithLayoutAsync(layout).ConfigureAwait(false);
}

/// <summary>
/// Reconciles the current adapter tree with the provided layout.
/// Adds/removes/reorders adapters without full reconstruction.
/// Preserves expand/collapse state and internal state.
/// </summary>
private async Task ReconcileAdaptersWithLayoutAsync(IList<ExplorerEntryData> layout)
{
    var existingChildren = await this.Children.ConfigureAwait(false);
    var seenIds = new HashSet<Guid>();
    var targetAdapters = new List<ITreeItem>();

    // Build target adapters from layout
    foreach (var entry in layout)
    {
        var adapter = await this.BuildAdapterFromLayoutEntryAsync(entry, seenIds)
            .ConfigureAwait(false);
        if (adapter is not null)
        {
            targetAdapters.Add(adapter);
        }
    }

    // Now reconcile: remove adapters not in target, add missing, reorder as needed
    // Preserve adapters that still exist (keeping their state)

    // Remove adapters not in target layout
    for (int i = existingChildren.Count - 1; i >= 0; --i)
    {
        var existing = existingChildren[i];
        if (!targetAdapters.Contains(existing))
        {
            await this.RemoveChildAsync((TreeItemAdapter)existing).ConfigureAwait(false);
        }
    }

    // Reorder and add adapters
    for (int i = 0; i < targetAdapters.Count; ++i)
    {
        var targetAdapter = targetAdapters[i];
        if (!existingChildren.Contains(targetAdapter))
        {
            // New adapter, add it
            this.AddChildInternal((TreeItemAdapter)targetAdapter);
        }
        // If exists, check if reorder needed (future optimization)
    }
}

/// <summary>
/// Builds or retrieves an adapter for a layout entry, reusing existing adapters where possible.
/// </summary>
private async Task<ITreeItem?> BuildAdapterFromLayoutEntryAsync(
    ExplorerEntryData entry,
    HashSet<Guid> seenIds)
{
    switch (entry.Type)
    {
        case "Folder" when entry.FolderId is not null:
            var folder = new FolderAdapter(entry.FolderId.Value, entry.Name ?? "Folder");
            if (entry.Children is not null)
            {
                foreach (var childEntry in entry.Children)
                {
                    var childAdapter = await this.BuildAdapterFromLayoutEntryAsync(childEntry, seenIds)
                        .ConfigureAwait(false);
                    if (childAdapter is not null)
                    {
                        folder.AddChildAdapter(childAdapter);
                    }
                }
            }
            return folder;

        case "Node" when entry.NodeId is not null:
            seenIds.Add(entry.NodeId.Value);
            var node = this.AttachedObject.AllNodes.FirstOrDefault(n => n.Id == entry.NodeId);
            return node is not null ? new SceneNodeAdapter(node) : null;

        default:
            return null;
    }
}
```

**Benefits**:
- Expand/collapse state preserved
- Scroll position preserved
- Faster (no full tree rebuild)
- Adapter identity preserved for debugging

### Recommendation 2.2: Avoid Double Reconstruction on Undo

Fix the CreateFolderFromSelection undo:

```csharp
// CURRENT: Calls ReloadChildrenAsync + InitializeRootAsync (redundant!)
UndoRedo.Default[this].AddChange(
    $"Create Folder ({folder.Name})",
    () => {
        this.suppressUndoRecording = true;
        try {
            scene.ExplorerLayout = previousLayout;
            sceneAdapter.ReloadChildrenAsync().GetAwaiter().GetResult();      // Full rebuild
            this.InitializeRootAsync(sceneAdapter, skipRoot: false)
                .GetAwaiter().GetResult();  // Redundant rebuild!
        }
        finally {
            this.suppressUndoRecording = false;
        }
    });

// IMPROVED: Single reconciliation
UndoRedo.Default[this].AddChange(
    $"Create Folder ({folder.Name})",
    () => {
        this.suppressUndoRecording = true;
        try {
            scene.ExplorerLayout = previousLayout;

            // Single operation: reconcile adapters with new layout
            // Preserves expand/collapse state
            sceneAdapter.RefreshLayoutAsync().GetAwaiter().GetResult();
        }
        finally {
            this.suppressUndoRecording = false;
        }
    });
```

### Recommendation 2.3: Validate Layout Consistency

Before applying layout, validate it:

```csharp
/// <summary>
/// Validates that a layout is consistent with the scene model.
/// </summary>
public class ExplorerLayoutValidator
{
    public ValidationResult Validate(IList<ExplorerEntryData>? layout, Scene scene)
    {
        var errors = new List<string>();

        if (layout is null)
            return ValidationResult.Valid;

        var nodeIds = new HashSet<Guid>(scene.AllNodes.Select(n => n.Id));
        this.ValidateLayoutEntries(layout, nodeIds, "", errors);

        return errors.Count == 0
            ? ValidationResult.Valid
            : new ValidationResult { Errors = errors };
    }

    private void ValidateLayoutEntries(
        IList<ExplorerEntryData>? entries,
        HashSet<Guid> validNodeIds,
        string path,
        List<string> errors)
    {
        if (entries is null)
            return;

        foreach (var entry in entries)
        {
            var entryPath = $"{path}/{entry.Type}:{entry.FolderId ?? entry.NodeId}";

            switch (entry.Type)
            {
                case "Node" when entry.NodeId is null:
                    errors.Add($"Node entry at {entryPath} has null NodeId");
                    break;

                case "Node" when !validNodeIds.Contains(entry.NodeId.Value):
                    errors.Add($"Node entry at {entryPath} references non-existent node {entry.NodeId}");
                    break;

                case "Folder" when entry.FolderId is null:
                    errors.Add($"Folder entry at {entryPath} has null FolderId");
                    break;

                case "Folder":
                    this.ValidateLayoutEntries(entry.Children, validNodeIds, entryPath, errors);
                    break;
            }
        }
    }
}
```

Use in undo:
```csharp
UndoRedo.Default[this].AddChange(
    $"Create Folder ({folder.Name})",
    () => {
        scene.ExplorerLayout = previousLayout;

        // ADDED: Validate before applying
        var validation = layoutValidator.Validate(previousLayout, scene);
        if (!validation.IsValid)
        {
            this.logger.LogWarning("Restored layout is invalid: {Errors}",
                string.Join("; ", validation.Errors));
            // Could fall back to flat RootNodes
            scene.ExplorerLayout = null;
        }

        sceneAdapter.RefreshLayoutAsync().GetAwaiter().GetResult();
    });
```

### Recommendation 2.4: Fail-Safe Undo Actions

Catch and report undo failures instead of silently swallowing:

```csharp
/// <summary>
/// Adds a change to the undo stack with exception handling that prevents silent failures.
/// </summary>
public void AddChangeWithFailSafety(string description, Action action)
{
    UndoRedo.Default[this].AddChange(description, () => {
        try
        {
            action();
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "Undo action '{Description}' failed. State may be inconsistent.", description);

            // Show user-facing warning
            this.messenger.Send(new UndoFailedMessage(description, ex.Message));

            // Optionally: attempt recovery
            this.AttemptRecovery();

            // Re-throw so undo system knows it failed
            throw;
        }
    });
}

// In OnItemAdded:
this.AddChangeWithFailSafety(
    $"RemoveItem({item.Label})",
    () => this.RemoveItemAsync(item).GetAwaiter().GetResult());
```

### Recommendation 2.5: Granular Undo for Complex Operations

Instead of one big undo entry for folder creation, use a changeset:

```csharp
[RelayCommand(CanExecute = nameof(HasUnlockedSelectedItems))]
private async Task CreateFolderFromSelection()
{
    // ... setup code ...

    // Group all operations under one changeset
    var changeSet = UndoRedo.Default[this].BeginChangeSet($"Create Folder ({folder.Name})");

    try
    {
        this.suppressUndoRecording = false;  // Allow per-operation undo entries

        // Now each individual operation records its own undo entry,
        // but they're grouped under the changeset

        // Remove nodes from layout
        // (RemoveItemAsync triggers OnItemRemoved → undo entry)

        // Insert folder
        // (InsertItemAsync triggers OnItemAdded → undo entry)

        // Rearrange adapters
        // (Multiple calls, each tracked)
    }
    finally
    {
        changeSet.Dispose();  // Closes the changeset group
    }
}
```

This way, undo of "Create Folder" naturally undoes all the individual operations in reverse.

---

## Part 3: Improving UX (Tree Jumps, Collapses, Nested Folder Bugs)

### Current Problems

1. **Tree Jumps**: `ReloadChildrenAsync()` clears all adapters then rebuilds, causing:
   - Scroll position reset
   - Expansion state loss
   - Visual flicker/jump

2. **Folder Collapses**: When adapters are reconstructed, `IsExpanded` is reset to `false`
   - User expands a folder
   - User creates a folder elsewhere
   - Tree reloads all adapters
   - User's expanded folder is now collapsed!

3. **Nested Folder Bug**: Creating a folder inside a folder fails
   - Code tries to find folder entry recursively but doesn't properly add to nested folder's children
   - Node adapters aren't found under nested folder adapters
   - `FindAdapterForNodeIdAsync` can't locate nodes inside nested folders

4. **Expansion State Loss on Undo**: No mechanism to preserve/restore expand/collapse across undo

### Recommendation 3.1: Preserve Expansion State Across Rebuilds

Create an expansion state snapshot:

```csharp
/// <summary>
/// Captures the expand/collapse state of the entire tree.
/// </summary>
public class TreeExpansionState
{
    // Maps adapter identity to IsExpanded state
    // Using node ID + parent path to identify adapters uniquely
    public Dictionary<string, bool> ExpandedItems { get; } = new();

    /// <summary>
    /// Captures current expansion state from the tree.
    /// </summary>
    public static async Task<TreeExpansionState> CaptureAsync(ITreeItem root)
    {
        var state = new TreeExpansionState();
        await CaptureRecursiveAsync(root, "", state).ConfigureAwait(false);
        return state;
    }

    private static async Task CaptureRecursiveAsync(
        ITreeItem item,
        string parentPath,
        TreeExpansionState state)
    {
        var itemKey = GetItemKey(item, parentPath);

        if (item is TreeItemAdapter adapter)
        {
            state.ExpandedItems[itemKey] = adapter.IsExpanded;
        }

        var children = await item.Children.ConfigureAwait(false);
        foreach (var child in children)
        {
            await CaptureRecursiveAsync(child, itemKey, state).ConfigureAwait(false);
        }
    }

    /// <summary>
    /// Restores expansion state to the tree.
    /// </summary>
    public async Task RestoreAsync(ITreeItem root)
    {
        await RestoreRecursiveAsync(root, "", this).ConfigureAwait(false);
    }

    private static async Task RestoreRecursiveAsync(
        ITreeItem item,
        string parentPath,
        TreeExpansionState state)
    {
        var itemKey = GetItemKey(item, parentPath);

        if (item is TreeItemAdapter adapter && state.ExpandedItems.TryGetValue(itemKey, out var expanded))
        {
            adapter.IsExpanded = expanded;
        }

        var children = await item.Children.ConfigureAwait(false);
        foreach (var child in children)
        {
            await RestoreRecursiveAsync(child, itemKey, state).ConfigureAwait(false);
        }
    }

    private static string GetItemKey(ITreeItem item, string parentPath)
    {
        var id = item switch
        {
            SceneNodeAdapter sna => sna.AttachedObject.Id.ToString(),
            FolderAdapter fa => fa.Id.ToString(),
            SceneAdapter _ => "scene-root",
            _ => item.GetHashCode().ToString()
        };

        return $"{parentPath}/{id}";
    }
}
```

Use in folder creation:

```csharp
private async Task CreateFolderFromSelection()
{
    // Capture expansion state BEFORE any changes
    var expansionState = await TreeExpansionState.CaptureAsync(this.Scene!)
        .ConfigureAwait(false);

    // ... create folder logic ...

    // Store both layout and expansion state for undo
    var previousLayout = CloneLayout(scene.ExplorerLayout);
    var previousExpansionState = expansionState;

    // ... insert adapters ...

    // Restore expansion state after changes
    await previousExpansionState.RestoreAsync(this.Scene!)
        .ConfigureAwait(false);

    // Undo restores both layout AND expansion state
    UndoRedo.Default[this].AddChange(
        $"Create Folder ({folder.Name})",
        () => {
            scene.ExplorerLayout = previousLayout;

            // Restore layout first
            sceneAdapter.RefreshLayoutAsync().GetAwaiter().GetResult();

            // Then restore expansion state
            previousExpansionState.RestoreAsync(sceneAdapter)
                .GetAwaiter().GetResult();
        });
}
```

### Recommendation 3.2: Fix Nested Folder Bug by Improving Adapter Lookup

The current `FindAdapterForNodeIdAsync` doesn't properly handle nested folder structures:

```csharp
// CURRENT: Incomplete - doesn't load children of folders before searching
private static async Task<SceneNodeAdapter?> FindAdapterForNodeIdAsync(ITreeItem root, System.Guid nodeId)
{
    if (root is SceneNodeAdapter sna && sna.AttachedObject.Id == nodeId)
        return sna;

    var children = await root.Children.ConfigureAwait(true);
    foreach (var child in children)
    {
        if (child is SceneNodeAdapter childNode && childNode.AttachedObject.Id == nodeId)
            return childNode;

        var nested = await FindAdapterForNodeIdAsync(child, nodeId).ConfigureAwait(true);
        if (nested is not null)
            return nested;
    }

    return null;
}

// IMPROVED: Explicitly initializes folder children to find nested adapters
private static async Task<SceneNodeAdapter?> FindAdapterForNodeIdAsync(
    ITreeItem root,
    Guid nodeId)
{
    // Check this item
    if (root is SceneNodeAdapter sna && sna.AttachedObject.Id == nodeId)
    {
        return sna;
    }

    // Load children (critical for FolderAdapter which may not have initialized yet)
    if (root is TreeItemAdapter ta && !ta.AreChildrenLoaded)
    {
        await ta.InitializeChildrenCollectionAsync().ConfigureAwait(false);
    }

    var children = await root.Children.ConfigureAwait(false);

    foreach (var child in children)
    {
        // Direct match
        if (child is SceneNodeAdapter childNode && childNode.AttachedObject.Id == nodeId)
        {
            return childNode;
        }

        // Recursive search (also covers nested folders)
        var found = await FindAdapterForNodeIdAsync(child, nodeId).ConfigureAwait(false);
        if (found is not null)
        {
            return found;
        }
    }

    return null;
}
```

### Recommendation 3.3: Fix Nested Folder Creation by Properly Updating Layout

The bug: nested folder entries don't get proper children initialization:

```csharp
// CURRENT (from SceneAdapter.LoadChildren):
// This code doesn't properly handle deeply nested folders
if (childEntry.Type == "Folder")
{
    var subFolder = new FolderAdapter(subFolderId, childEntry.Name);

    if (childEntry.Children is not null)
    {
        foreach (var nested in childEntry.Children)
        {
            // Only handles immediate children, not nested folders' children properly
            if (nested.Type == "Node")
                subFolder.AddChildAdapter(nodeAdapter);
        }
    }

    folder.AddChildAdapter(subFolder);  // subFolder children not fully loaded!
}

// IMPROVED: Use a unified recursive function
private async Task<FolderAdapter> BuildFolderAdapterAsync(ExplorerEntryData entry)
{
    Debug.Assert(entry.Type == "Folder", "Expected folder entry");

    var folder = new FolderAdapter(entry.FolderId!.Value, entry.Name ?? "Folder")
    {
        IsExpanded = false
    };

    if (entry.Children is null)
        return folder;

    foreach (var childEntry in entry.Children)
    {
        var childAdapter = await this.BuildAdapterFromLayoutEntryRecursiveAsync(childEntry)
            .ConfigureAwait(false);

        if (childAdapter is not null && childAdapter is TreeItemAdapter ta)
        {
            folder.AddChildAdapter(ta);
        }
    }

    return folder;
}

private async Task<ITreeItem?> BuildAdapterFromLayoutEntryRecursiveAsync(ExplorerEntryData entry)
{
    switch (entry.Type)
    {
        case "Folder" when entry.FolderId is not null:
            return await this.BuildFolderAdapterAsync(entry).ConfigureAwait(false);

        case "Node" when entry.NodeId is not null:
            var node = this.AttachedObject.AllNodes.FirstOrDefault(n => n.Id == entry.NodeId);
            return node is not null ? new SceneNodeAdapter(node) : null;

        default:
            return null;
    }
}
```

Now use in `LoadChildren`:

```csharp
protected override async Task LoadChildren()
{
    this.ClearChildren();

    var layout = this.AttachedObject.ExplorerLayout;
    var seenNodeIds = new HashSet<Guid>();

    if (layout is not null && layout.Count > 0)
    {
        foreach (var entry in layout)
        {
            var adapter = await this.BuildAdapterFromLayoutEntryRecursiveAsync(entry)
                .ConfigureAwait(false);

            if (adapter is not null && adapter is TreeItemAdapter ta)
            {
                this.AddChildInternal(ta);

                // Track seen nodes for backward compatibility
                if (entry.Type == "Node" && entry.NodeId is not null)
                {
                    seenNodeIds.Add(entry.NodeId.Value);
                }
            }
        }
    }

    // Backward compatibility: add unseen root nodes
    foreach (var rootNode in this.AttachedObject.RootNodes)
    {
        if (!seenNodeIds.Contains(rootNode.Id))
        {
            this.AddChildInternal(new SceneNodeAdapter(rootNode) { IsExpanded = false });
        }
    }

    await Task.CompletedTask.ConfigureAwait(false);
}
```

### Recommendation 3.4: Prevent Unintended Folder Collapses on Operations

Add a flag to prevent automatic layout reload during certain operations:

```csharp
// Add to ViewModel
private bool shouldPreserveTreeState;

/// <summary>
/// Executes an operation while preserving tree expansion and scroll state.
/// </summary>
public async Task ExecuteWithStatePreservationAsync(Func<Task> operation)
{
    // Capture current state
    var expansionState = await TreeExpansionState.CaptureAsync(this.Scene!)
        .ConfigureAwait(false);
    var scrollOffset = this.TreeControl?.GetCurrentScrollOffset() ?? 0;  // Requires TreeControl integration

    try
    {
        this.shouldPreserveTreeState = true;
        await operation().ConfigureAwait(false);
    }
    finally
    {
        this.shouldPreserveTreeState = false;

        // Restore state
        await expansionState.RestoreAsync(this.Scene!)
            .ConfigureAwait(false);
        this.TreeControl?.ScrollToOffset(scrollOffset);
    }
}

// Use when needed:
await this.ExecuteWithStatePreservationAsync(async () =>
{
    var selectedItem = this.SelectionModel?.SelectedItem;
    await this.AddEntity().ConfigureAwait(false);
});
```

### Recommendation 3.5: Add Progress Indication for Slow Operations

For operations that might cause delays (large folder creation):

```csharp
[RelayCommand(CanExecute = nameof(HasUnlockedSelectedItems))]
private async Task CreateFolderFromSelection()
{
    using var progressScope = this.logger.BeginScope("CreateFolderFromSelection");

    this.IsProcessingOperation = true;  // Disable UI
    try
    {
        // ... all existing logic ...
    }
    finally
    {
        this.IsProcessingOperation = false;
    }
}

[ObservableProperty]
public partial bool IsProcessingOperation { get; set; }
```

In XAML:
```xml
<Grid IsEnabled="{Binding !IsProcessingOperation}">
    <!-- Tree content -->
</Grid>
```

---

## Summary Table

| Issue | Root Cause | Recommendation | Benefit |
|-------|-----------|-----------------|---------|
| Unclear UI vs scene distinction | Implicit logic scattered across handlers | Semantic operation types + dispatcher | Compile-time safety, clarity |
| Folder collapse on undo | Full adapter rebuild loses state | Preserve expansion state with snapshot | User expectations met |
| Tree jumps | Clearing all adapters then rebuilding | Reconcile in-place with `RefreshLayoutAsync()` | Smooth UX, state preserved |
| Nested folder bug | Incomplete recursive folder building | Use unified recursive adapter building | Nesting works at any depth |
| Double reconstruction | ReloadChildren + InitializeRoot | Use single `RefreshLayoutAsync()` | Faster, cleaner undo |
| Silent undo failures | Swallowed exceptions | Fail-safe undo with messaging | User knows when undo fails |
| Node lookup fails in nested folders | Doesn't initialize folder children first | Explicit child initialization in lookup | Finds nodes at any depth |
| Layout becomes inconsistent | No validation | Validate layout before applying | Prevents broken state |

---

## Implementation Priority

### Phase 1 (Critical - High Impact)
1. Fix nested folder bug (Rec 3.3)
2. Preserve expansion state (Rec 3.1)
3. Use `RefreshLayoutAsync()` instead of double reload (Rec 2.1, 2.2)

### Phase 2 (Important - Medium Impact)
4. Semantic operation types (Rec 1.1)
5. Separate event handlers by type (Rec 1.4)
6. Fix nested folder lookup (Rec 3.2)

### Phase 3 (Nice - Lower Priority)
7. Operation dispatcher (Rec 1.2)
8. Layout validation (Rec 2.3)
9. Fail-safe undo (Rec 2.4)
10. State preservation wrapper (Rec 3.4)
11. Progress indication (Rec 3.5)

---

## Testing Strategy

For each improvement, add tests for:

```csharp
[TestClass]
public class NestedFolderCreationTests
{
    [TestMethod]
    public async Task CreateFolderUnderFolder_FindsNodesAtAnyDepth()
    {
        // Arrange: create nested folder structure
        var scene = new Scene();
        var node = new SceneNode(scene) { Name = "TestNode" };

        var layout = new List<ExplorerEntryData>
        {
            new() {
                Type = "Folder",
                FolderId = Guid.NewGuid(),
                Name = "Outer",
                Children = new List<ExplorerEntryData>
                {
                    new() {
                        Type = "Folder",
                        FolderId = Guid.NewGuid(),
                        Name = "Inner",
                        Children = new List<ExplorerEntryData>
                        {
                            new() { Type = "Node", NodeId = node.Id }
                        }
                    }
                }
            }
        };
        scene.ExplorerLayout = layout;
        var adapter = new SceneAdapter(scene);

        // Act: find node in nested folder
        var found = await FindAdapterForNodeIdAsync(adapter, node.Id);

        // Assert
        Assert.IsNotNull(found);
        Assert.AreEqual(node.Id, ((SceneNodeAdapter)found!).AttachedObject.Id);
    }

    [TestMethod]
    public async Task Undo_RestoresExpansionState()
    {
        // Arrange
        var viewModel = CreateViewModel();
        await viewModel.LoadSceneAsync(testScene);

        // Expand a folder
        var folderAdapter = viewModel.Scene!.Children.First(x => x is FolderAdapter);
        folderAdapter.IsExpanded = true;

        // Create another folder (triggers reload)
        await viewModel.CreateFolderFromSelection();

        // Assert: folder is still expanded
        Assert.IsTrue(folderAdapter.IsExpanded);

        // Undo
        viewModel.UndoCommand.Execute(null);

        // Assert: expansion state preserved
        var refoundFolder = viewModel.Scene!.Children.First(x => x is FolderAdapter);
        Assert.IsTrue(refoundFolder.IsExpanded);
    }
}
```

---

## Conclusion

These recommendations address the core issues by:

1. **Making intent explicit** with semantic types
2. **Preserving state** instead of destroying and rebuilding
3. **Separating concerns** clearly (scene vs layout)
4. **Validating** before applying changes
5. **Testing thoroughly** at each layer

Implementation of Phase 1 should immediately resolve the most visible UX issues while laying groundwork for long-term maintainability.
