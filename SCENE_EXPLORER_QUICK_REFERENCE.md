# Scene Explorer: Quick Reference for Improvements

## The Three Major Issues

### 1. Implicit UI/Scene Distinction
**Problem**: Code mixes layout operations (UI folders) with scene operations (engine sync)
**Impact**: Easy to forget engine sync, hard to review
**Fix**: Use semantic operation types + separate handlers

### 2. Tree Collapses & Jumps
**Problem**: Operations rebuild entire adapter tree, losing expansion state
**Impact**: User expands a folder, then it collapses when they do something else
**Fix**: Preserve expansion state + reconcile in-place instead of rebuild

### 3. Nested Folder Creation Broken
**Problem**: Can't create folders inside folders; nodes aren't found
**Impact**: Feature partially broken; workarounds required
**Fix**: Fix recursive folder building + explicit child initialization

---

## Quick Wins (Phase 1)

### Win #1: Fix Nested Folder Bug (2-4 hours)
**File**: `SceneAdapter.cs`
**Change**: Use unified recursive function for building adapters
**Impact**: Nested folders work at any depth

```csharp
// Replace the nested if/else spaghetti with:
private async Task<ITreeItem?> BuildAdapterFromLayoutEntryRecursiveAsync(ExplorerEntryData entry)
{
    switch (entry.Type)
    {
        case "Folder" when entry.FolderId is not null:
            var folder = new FolderAdapter(entry.FolderId.Value, entry.Name ?? "Folder");
            if (entry.Children is not null)
            {
                foreach (var childEntry in entry.Children)
                {
                    var childAdapter = await this.BuildAdapterFromLayoutEntryRecursiveAsync(childEntry)
                        .ConfigureAwait(false);
                    if (childAdapter is not null && childAdapter is TreeItemAdapter ta)
                    {
                        folder.AddChildAdapter(ta);
                    }
                }
            }
            return folder;

        case "Node" when entry.NodeId is not null:
            var node = this.AttachedObject.AllNodes.FirstOrDefault(n => n.Id == entry.NodeId);
            return node is not null ? new SceneNodeAdapter(node) : null;

        default:
            return null;
    }
}
```

### Win #2: Preserve Expansion State (1 day)
**Files**:
- Create: `TreeExpansionState.cs`
- Modify: `SceneExplorerViewModel.cs` CreateFolderFromSelection method

**Impact**:
- Expansion state preserved across undo
- No more unexpected collapses
- User's tree organization stays intact

```csharp
// New class to capture/restore expansion
public class TreeExpansionState
{
    public Dictionary<string, bool> ExpandedItems { get; } = new();

    public static async Task<TreeExpansionState> CaptureAsync(ITreeItem root)
    {
        // Recursively capture IsExpanded for all adapters
    }

    public async Task RestoreAsync(ITreeItem root)
    {
        // Recursively restore IsExpanded for all adapters
    }
}

// Use in folder creation:
var expansionState = await TreeExpansionState.CaptureAsync(this.Scene);
// ... perform operations ...
await expansionState.RestoreAsync(this.Scene);

// In undo:
UndoRedo.Default[this].AddChange("Create Folder", () => {
    scene.ExplorerLayout = previousLayout;
    sceneAdapter.RefreshLayoutAsync();
    previousExpansionState.RestoreAsync(sceneAdapter);
});
```

### Win #3: Stop Tree Jumps (1-2 hours)
**File**: `SceneAdapter.cs`
**Change**: Add `RefreshLayoutAsync()` method
**Impact**: Smooth tree updates, no jumping

```csharp
/// <summary>
/// Reloads layout WITHOUT recreating adapters, preserving expand/collapse state.
/// </summary>
public async Task RefreshLayoutAsync()
{
    var layout = this.AttachedObject.ExplorerLayout;
    if (layout is null || layout.Count == 0)
    {
        await this.ReloadChildrenAsync().ConfigureAwait(false);
        return;
    }

    // Mutate existing tree to match layout (preserves state)
    await this.ReconcileAdaptersWithLayoutAsync(layout).ConfigureAwait(false);
}
```

Use instead of `ReloadChildrenAsync()`:
```csharp
// BEFORE (causes jumps)
sceneAdapter.ReloadChildrenAsync();

// AFTER (smooth)
sceneAdapter.RefreshLayoutAsync();
```

---

## Medium Effort (Phase 2)

### Semantic Operation Types
**Cost**: 4-6 hours
**Impact**: Compiler enforces scene vs layout, clearer code

```csharp
// Create types that declare intent
public abstract record SceneOperation;  // Requires engine sync
public sealed record CreateSceneNodeOperation(SceneNode Node, Guid? ParentNodeId) : SceneOperation;

public abstract record LayoutOperation;  // UI-only
public sealed record CreateFolderOperation(...) : LayoutOperation;
```

### Separate Event Handlers
**Cost**: 2-3 hours
**Impact**: Clearer control flow, less bugs

```csharp
// Instead of giant if/else, dispatch to semantic handlers:
private async void OnItemBeingAdded(TreeItemBeingAddedEventArgs args)
{
    switch (args.Parent)
    {
        case SceneNodeAdapter nodeParent:
            await OnSceneNodeBeingReparentedToNode(...);
        case SceneAdapter sceneParent:
            await OnSceneNodeBeingAddedToScene(...);
        case FolderAdapter folder:
            await OnSceneNodeBeingMovedToFolder(...);
    }
}

// Each handler is focused and clear
private async Task OnSceneNodeBeingAddedToScene(SceneNodeAdapter adapter, SceneAdapter scene)
{
    // ONLY: scene mutations + engine sync + messaging
    // NO: layout-only logic
}
```

---

## Implementation Order

```
Week 1:
  Mon-Tue: Fix nested folder bug + preserve expansion state
  Wed:     Add RefreshLayoutAsync, fix double reconstruction
  Thu-Fri: Testing, refinement

Week 2:
  Mon-Tue: Semantic operation types
  Wed:     Separate event handlers
  Thu-Fri: Testing, documentation
```

---

## Testing

For each fix, test:

```csharp
[TestMethod]
public async Task NestedFolders_CreatedSuccessfully()
{
    // Create folder in folder in folder
    // Verify nodes found at all depths
    // Verify layout structure correct
}

[TestMethod]
public async Task ExpansionState_PreservedAcrossUndo()
{
    // Expand folder
    // Create another folder
    // Assert first folder still expanded
    // Undo
    // Assert expansion preserved
}

[TestMethod]
public async Task TreeDoesNotJump_WhenRefreshingLayout()
{
    // Get scroll position
    // Refresh layout
    // Assert scroll position unchanged
}
```

---

## Files to Create/Modify

### Create
- `TreeExpansionState.cs` - Capture/restore expansion state
- Optionally: `SceneOperation.cs`, `LayoutOperation.cs` - Semantic types

### Modify
- `SceneAdapter.cs` - Add `RefreshLayoutAsync()`, `ReconcileAdaptersWithLayoutAsync()`
- `SceneExplorerViewModel.cs` - Use `RefreshLayoutAsync()`, preserve expansion
- `FolderAdapter.cs` - Improved nested folder support

---

## Validation Checklist

### Phase 1 Complete When:
- [ ] Can create folder inside folder at any nesting level
- [ ] Nodes found at any depth in tree
- [ ] User expands folder → no unexpected collapse
- [ ] No tree jumps/scroll reset on operations
- [ ] Expansion preserved on undo/redo

### Phase 2 Complete When:
- [ ] New code clearly distinguishes scene vs layout ops
- [ ] Event handlers separated by operation type
- [ ] Code review easier (can see intent at a glance)
- [ ] No performance regression

---

## Rollback Plan

If any Phase 1 change causes issues:

1. **Revert** the change
2. **Log** the specific failure
3. **Discuss** root cause
4. **Try** alternative approach or break into smaller steps

All changes are localized and can be reverted individually.

---

## Questions Before Starting

1. **Expansion State**: Should folders stay expanded when user does other operations?
2. **Nested Folders**: How deep should nesting support go? (unlimited recommended)
3. **Performance**: Any concerns about preserving expansion state? (O(n) traversal, minimal)
4. **Timeline**: Can Phase 1 be completed before next release?

---

## Success Criteria

After implementation:

- ✓ No more "my folder collapsed!" complaints
- ✓ No more "I can't create folder in folder" issues
- ✓ Tree stays in user's scrolled position
- ✓ Nested folders work at any depth
- ✓ Code clearly separates scene from layout concerns

---

## Reference

See detailed recommendations:
- `SCENE_EXPLORER_IMPROVEMENTS.md` - Full recommendations with code examples
- `SCENE_EXPLORER_ANALYSIS.md` - Original design analysis
