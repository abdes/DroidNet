# Scene Explorer: Design and Implementation Analysis

## Executive Summary

The Scene Explorer is a sophisticated hierarchical UI component that manages scene composition in the Oxygen Editor. It implements a carefully layered architecture that separates **UI presentation concerns** from **model mutations** from **engine synchronization**, with explicit control flow through event handlers, semantic commands, and messaging protocols.

Key architectural insight: **The explorer tracks two distinct hierarchies** — the runtime scene graph (parent-child relationships between SceneNodes) and the UI layout (folders, groupings, and display order). These are synchronized but distinct, allowing flexible UI organization independent of rendering hierarchy.

---

## Architecture Overview

### Three-Layer Design Pattern

```
┌─────────────────────────────────────────────────────────────┐
│ PRESENTATION LAYER                                          │
│ SceneExplorerView (XAML + Code-Behind)                     │
│ - Handles keyboard accelerators (Undo, Redo, Delete)      │
│ - Delegates to ViewModel commands                          │
│ - Provides toolbar/menu integration points                 │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│ VIEW MODEL LAYER                                            │
│ SceneExplorerViewModel (MVVM ViewModel)                    │
│ - Commands: AddEntity, RemoveSelectedItems, Undo/Redo    │
│ - Selection Management (Single & Multiple modes)          │
│ - Tree Mutation Orchestration                             │
│ - Model Operations (AddItem, RemoveItem, InsertItem)      │
└────────────────────────┬────────────────────────────────────┘
                         │
        ┌────────────────┼────────────────┐
        │                │                │
┌───────▼────────┐ ┌─────▼──────┐ ┌─────▼──────────────┐
│ ADAPTER LAYER  │ │ MODEL LAYER│ │ SERVICE LAYER     │
│                │ │            │ │                   │
│ SceneAdapter   │ │ Scene      │ │ ISceneEngineSync  │
│ SceneNodeAdptr │ │ SceneNode  │ │ IMessenger        │
│ FolderAdapter  │ │ ExplorerL. │ │ IRouter/IRouter   │
└────────────────┘ └────────────┘ └───────────────────┘
```

### Key Components

1. **SceneExplorerView**: Entry point for user interactions
   - Keyboard accelerators (Ctrl+Z, Ctrl+Y, Delete)
   - Toolbar button callbacks
   - Delegates to ViewModel commands
   - Minimal code-behind, follows MVVM pattern

2. **SceneExplorerViewModel**: Orchestrates UI state and model mutations
   - Inherits from `DynamicTreeViewModel` (base tree control)
   - Manages tree items (adapters) and selection
   - Handles event lifecycle: `OnItemBeingAdded/OnItemAdded/OnItemBeingRemoved/OnItemRemoved`
   - Coordinates model mutations with engine sync

3. **Adapter Layer**: Bridges UI tree to runtime models
   - `SceneAdapter`: Wraps the Scene root
   - `SceneNodeAdapter`: Wraps individual SceneNode entities
   - `FolderAdapter`: UI-only construct (no model representation)

4. **Service Layer**: External integrations
   - `ISceneEngineSync`: Propagates changes to native engine
   - `IMessenger`: Cross-component messaging (MVVM Toolkit)
   - `IRouter`: Navigation events

---

## The Two-Hierarchy Model

### Runtime Scene Graph (Model)

The canonical scene structure stored in the Scene:
```
Scene (root)
├── SceneNode 1 (parent-child relationships via node.Parent)
│   ├── SceneNode 1.1
│   └── SceneNode 1.2
├── SceneNode 2
└── SceneNode 3
```

- Stored in `Scene.RootNodes` (top-level entities only)
- Full hierarchy via `SceneNode.Parent` and `AllNodes` traversal
- **Only modified when runtime mutations occur** (create, delete, reparent)
- This is what the engine knows about

### UI Explorer Layout (ExplorerLayout)

A cached, editor-specific organization structure:
```
ExplorerLayout
├── Folder A (no model representation!)
│   ├── SceneNode 1 (reference by ID)
│   └── Folder B
│       └── SceneNode 1.1
├── Folder C
└── SceneNode 2 (loose reference)
```

- Stored in `Scene.ExplorerLayout` (type: `IList<ExplorerEntryData>`)
- Purely for **UI presentation** (folders, grouping, display order)
- Can be null (falls back to flat `Scene.RootNodes`)
- Recursively nested structure with Folder and Node entry types
- Does **not** affect runtime scene graph

### Why Two Hierarchies?

1. **Flexibility**: Users can organize display without constraining engine hierarchy
2. **Performance**: No model mutations for folder operations (UI-only)
3. **Separation of Concerns**: Rendering hierarchy ≠ Organization hierarchy
4. **Backward Compatibility**: Null layout automatically falls back to flat RootNodes

**Critical Principle**: Folder operations do NOT sync to engine. Only SceneNode create/delete/reparent operations sync.

---

## Control Flow: From User Action to Engine Sync

### User Action Flow Patterns

#### Pattern 1: Creating a New Entity

**Initiation** → **Model Mutation** → **Engine Sync** → **Messaging**

```
1. User clicks "Add Entity" or adds via context menu
2. ViewModel.AddEntity() executes (if CanAddEntity() returns true)
   - Gets selected parent (Scene or SceneNode)
   - Creates new SceneNode model with parent reference
   - Creates SceneNodeAdapter wrapper
   - Calls InsertItemAsync(relativeIndex, parent, newEntity)

3. DynamicTreeViewModel.InsertItemAsync() (base class)
   - Triggers TreeItemBeingAddedEventArgs
   → OnItemBeingAdded() handler (BEFORE tree modification)

4. OnItemBeingAdded() [KEY LOGIC]:
   a) Detects if addition is under SceneAdapter (root) or SceneNodeAdapter (parent)
   b) If under SceneAdapter:
      - Adds entity to Scene.RootNodes (model mutation #1)
      - If ExplorerLayout exists, adds Node entry (model mutation #2)
      - Calls sceneEngineSync.CreateNodeAsync() (engine sync #1)
      - Sends SceneNodeAddedMessage via messenger

   c) If under SceneNodeAdapter:
      - Calls entity.SetParent(parentNode) (model mutation)
      - Calls sceneEngineSync.CreateNodeAsync(entity, parentId) (engine sync)
      - Sends SceneNodeAddedMessage via messenger

   d) If under FolderAdapter:
      - Only updates ExplorerLayout (no model mutation!)
      - Calls sceneEngineSync.CreateNodeAsync(entity) (engine sync)
      - NO scene graph change, node remains in current parent

5. Tree finishes modification
   → Triggers TreeItemAddedEventArgs
   → OnItemAdded() handler (AFTER tree modification)

6. OnItemAdded():
   - Adds undo entry: UndoRedo.Default[this].AddChange()
   - Undo action safely removes item if still present
   - Logs item addition
   - No model mutations here

Result:
✓ Runtime scene graph updated
✓ Engine notified
✓ UI layout updated
✓ Undo stack updated
✓ Selection broadcasted (if applicable)
```

#### Pattern 2: Deleting Entities

**Initiation** → **Guard Check** → **Model Mutation** → **Engine Sync** → **Messaging**

```
1. User presses Delete or invokes RemoveSelectedItemsCommand

2. RemoveSelectedItems() [MVVM RelayCommand]:
   - Sets isPerformingDelete = true (guard flag)
   - Calls base.RemoveSelectedItems() (tree operation)
   - Sets isPerformingDelete = false
   - Wraps in undo changeset: UndoRedo.Default[this].BeginChangeSet()

3. DynamicTreeViewModel.RemoveSelectedItems() (base class)
   - Iterates selected items
   - For each item, triggers TreeItemBeingRemovedEventArgs
   → OnItemBeingRemoved() handler

4. OnItemBeingRemoved() [KEY LOGIC - GUARD BASED]:
   a) Captures old parent ONLY if NOT isPerformingDelete
      - Enables move operation detection later

   b) If isPerformingDelete == true:
      - Records adapter in deletedAdapters HashSet
      - Removes entity from Scene.RootNodes (model mutation #1)
      - Removes entries from ExplorerLayout (model mutation #2)
      - Calls sceneEngineSync.RemoveNodeAsync(entity.Id) (engine sync)

   c) If isPerformingDelete == false (drag/move):
      - Captures old parent reference
      - No model/engine changes here
      - Later add handler detects reparent

5. Tree finishes removal
   → Triggers TreeItemRemovedEventArgs
   → OnItemRemoved() handler

6. OnItemRemoved():
   - Adds undo entry
   - Checks if adapter was in deletedAdapters
   - Only then sends SceneNodeRemovedMessage
   - Cleans up capturedOldParent dictionary

Result:
✓ Scene.RootNodes updated
✓ ExplorerLayout entries removed
✓ Engine notified of removal
✓ Deletion message only sent if actual delete (not move)
✓ Move operations detect reparent via capturedOldParent
```

#### Pattern 3: Dragging/Reparenting Nodes

**Removal (capture parent)** → **Addition (detect reparent)** → **Model Mutation** → **Engine Sync**

```
1. User drags SceneNode from one parent to another

2. DynamicTreeViewModel removes item from old location
   → OnItemBeingRemoved()
      - isPerformingDelete == false (not a delete command)
      - Captures: capturedOldParent[adapter] = oldParentId

3. Tree finishes removal, no OnItemRemoved messaging yet

4. Tree processes insertion at new location
   → OnItemBeingAdded()
      a) Gets oldParentId from capturedOldParent dictionary
      b) Gets newParentId from args.Parent
      c) If oldParentId != newParentId:
         - Calls entity.SetParent(newParent) (model mutation)
         - Calls sceneEngineSync.ReparentNodeAsync(entity.Id, newParentId) (engine sync)
         - Sends SceneNodeReparentedMessage(entity, oldParentId, newParentId)
      d) Clears capturedOldParent[adapter]

5. Tree finishes addition
   → OnItemAdded()
      - Adds undo entry

Result:
✓ Parent-child relationship updated
✓ Engine maintains hierarchy consistency
✓ Single reparent message sent (not add + remove)
✓ Undo/Redo handles correctly
```

#### Pattern 4: Creating Folders (UI-Only Organization)

**Layout Mutation Only** → **Adapter Reorganization** → **No Engine Sync**

```
1. User invokes CreateFolderFromSelectionCommand

2. CreateFolderFromSelection():
   a) Captures selected node IDs from SelectionModel
      - No cached selection; always read live state
      - Fallback: inspect ShownItems with IsSelected flag

   b) Gets current ExplorerLayout from scene

   c) Clones layout for undo (deep recursive copy)

   d) Removes selected node entries from layout
      - Collects them into movedEntries list
      - Recursive removal (navigates nested folders)

   e) Creates new Folder entry with moved node entries
      - FolderId = Guid.NewGuid()
      - Type = "Folder"
      - Children = movedEntries

   f) Inserts folder at index 0 of layout
      - Updates Scene.ExplorerLayout = layout (model mutation #1)

   g) Suppresses undo recording (suppressUndoRecording = true)
      - Prevents duplicate undo entries during adapter reorganization

   h) Creates FolderAdapter and inserts via InsertItemAsync()
      - Finds and moves corresponding SceneNodeAdapters into folder
      - Updates tree UI to reflect layout change

   i) Expands folder and selects it for visual feedback

3. Adds single undo entry:
   UndoRedo.Default[this].AddChange("Create Folder", () => {
     scene.ExplorerLayout = previousLayout
     sceneAdapter.ReloadChildrenAsync()
   })

4. Redo entry pushed when undo executes:
   UndoRedo.Default[this].AddChange("Redo Create Folder", () => {
     scene.ExplorerLayout = layout
     sceneAdapter.ReloadChildrenAsync()
   })

Key Points:
✓ No SceneNode created/modified
✓ No engine sync call
✓ Only ExplorerLayout updated
✓ Adapter tree reorganized visually
✓ User sees immediate folder creation with contents
✓ Undo is a single atomic layout swap
```

---

## Event-Driven Architecture: The "Being" vs "After" Pattern

The ViewModel hooks into two complementary event pairs from DynamicTreeViewModel:

### Pre-Modification Events ("Being")

**TreeItemBeingAddedEventArgs** / **TreeItemBeingRemovedEventArgs**

- Fired **BEFORE** tree internal structures are updated
- Handlers can read old state and make model/engine decisions
- Exceptions here prevent the tree modification
- **Purpose**: Model mutations and engine sync happen here

**Example (OnItemBeingAdded)**:
```csharp
private async void OnItemBeingAdded(object? sender, TreeItemBeingAddedEventArgs args)
{
    // At this point:
    // - Tree hasn't added the item yet
    // - Old parent (if moving) is still valid
    // - We can read full old state for comparison

    var entity = args.TreeItem;
    var newParent = args.Parent;

    if (newParent is SceneNodeAdapter nodeParent)
    {
        var oldParentId = capturedOldParent.TryGetValue(entity, out var captured)
            ? captured
            : entity.Parent?.Id;

        // Make decision: reparent or create?
        entity.SetParent(nodeParent.AttachedObject);  // Model mutation
        await sceneEngineSync.ReparentNodeAsync(...);  // Engine sync
    }
}
```

### Post-Modification Events ("After")

**TreeItemAddedEventArgs** / **TreeItemRemovedEventArgs**

- Fired **AFTER** tree has completed its update
- Handlers see final state after modifications
- **Purpose**: Undo recording, messaging, cleanup

**Example (OnItemAdded)**:
```csharp
private void OnItemAdded(object? sender, TreeItemAddedEventArgs args)
{
    // At this point:
    // - Tree has added/updated the item
    // - Parent links are established
    // - All internal collections are consistent

    if (!suppressUndoRecording)
    {
        UndoRedo.Default[this].AddChange(...);  // Record undo
    }

    LogItemAdded(args.TreeItem.Label);
}
```

### Why This Pattern?

1. **Atomicity**: All model changes happen before tree updates (no stale state)
2. **Consistency**: Engine sync and model mutations happen together
3. **Rollback**: If "Being" handler throws, tree cancels its modification
4. **Undo**: "After" handler can safely record state (everything is settled)
5. **Messaging**: Send messages only when tree state is fully consistent

---

## State Management: Guards and Flags

The ViewModel maintains several transient flags to manage complex UI interactions:

### `isPerformingDelete` (bool)

**Purpose**: Distinguish deliberate delete from drag-move operations

**Flow**:
```
RemoveSelectedItemsCommand()
  isPerformingDelete = true
  base.RemoveSelectedItems()
    → OnItemBeingRemoved()
        if (isPerformingDelete)
          deletedAdapters.Add(adapter)
          remove from Scene.RootNodes
  isPerformingDelete = false

→ Later: OnItemRemoved()
    if (deletedAdapters.Remove(adapter))
      Send SceneNodeRemovedMessage()  // Only for actual deletes
```

**Why?**: DynamicTreeViewModel fires same events for move and delete. Flag allows conditional handling.

### `capturedOldParent: Dictionary<SceneNodeAdapter, Guid>` (transient)

**Purpose**: Detect reparent operations during move

**Flow**:
```
Drag starts:
  OnItemBeingRemoved()
    if (!isPerformingDelete)
      if (oldParent is SceneNodeAdapter)
        capturedOldParent[adapter] = oldParentId

Drag completes (new location):
  OnItemBeingAdded()
    oldParentId = capturedOldParent.TryGetValue(...)
    if (oldParentId != newParentId)
      ReparentNodeAsync(entity.Id, newParentId)
    capturedOldParent.Remove(adapter)
```

**Why?**: Without this, we couldn't distinguish "move under parent X" from "move under parent Y" — both look like additions.

### `suppressUndoRecording` (bool)

**Purpose**: Prevent duplicate undo entries during programmatic tree reorganizations

**Flow**:
```
CreateFolderFromSelection()
  suppressUndoRecording = true

  try:
    await InsertItemAsync(0, sceneAdapter, folderAdapter)  // No undo entry
    await RemoveItemAsync(nodeAdapter)                     // No undo entry
    await InsertItemAsync(folderAdapter.ChildrenCount, ...)// No undo entry
  finally:
    suppressUndoRecording = false

  // Create single atomic undo entry:
  UndoRedo.Default[this].AddChange("Create Folder", () => {
    scene.ExplorerLayout = previousLayout
    sceneAdapter.ReloadChildrenAsync()
  })
```

**Why?**: We perform multiple tree operations (remove, insert, etc.) as a unit. Without suppression, each would add separate undo entries, making undo unpredictable.

### `deletedAdapters: HashSet<SceneNodeAdapter>` (transient)

**Purpose**: Distinguish deleted adapters from moved adapters when removal completes

**Flow**:
```
Delete command:
  isPerformingDelete = true
  OnItemBeingRemoved()
    deletedAdapters.Add(adapter)
    RemoveNodeAsync(entity.Id)

  OnItemRemoved()
    if (deletedAdapters.Remove(adapter))  // Was deleted, not moved
      Send SceneNodeRemovedMessage()
```

**Why?**: Both delete and move trigger removal events. This flag allows selective messaging.

---

## Selection Management

### Selection Broadcasting Pattern

The ViewModel maintains real-time synchronization between tree selection and external components:

```csharp
public SelectionModel<ITreeItem>? SelectionModel { get; set; }

// Single selection mode
OnSingleSelectionChanged():
  selected = SelectionModel?.SelectedItem is SceneNodeAdapter adapter
    ? new[] { adapter.AttachedObject }
    : Array.Empty<SceneNode>()

  messenger.Send(new SceneNodeSelectionChangedMessage(selected))

// Multiple selection mode
OnMultipleSelectionChanged():
  selection = SelectedItems.Where(x => x is SceneNodeAdapter)
    .Select(x => ((SceneNodeAdapter)x).AttachedObject)
    .ToList()

  messenger.Send(new SceneNodeSelectionChangedMessage(selection))
```

**Key Points**:
- Selection changes immediately broadcast via messenger
- Other components (Properties panel, Transform gizmo) listen and update
- No cached selection — always read current state
- Supports both Single and Multiple selection modes
- Only `SceneNodeAdapter` items are sent (not folders)

### Selection Request Pattern

External components can query current selection:

```csharp
messenger.Register<SceneNodeSelectionRequestMessage>(this, OnSceneNodeSelectionRequested)

OnSceneNodeSelectionRequested(SceneNodeSelectionRequestMessage message):
  switch (SelectionModel):
    SingleSelectionModel:
      return current selected node or empty array
    MultipleSelectionModel:
      return all selected scene nodes (filter out folders)
```

**Use Case**: Other panels (e.g., Transform panel) request current selection without maintaining their own subscriptions.

---

## Undo/Redo System Integration

### Change Recording Strategy

The ViewModel uses a two-phase undo strategy:

**Phase 1: Per-Item Undo (Automatic)**
```csharp
OnItemAdded():
  if (!suppressUndoRecording):
    UndoRedo.Default[this].AddChange("RemoveItem(...)", () => {
      RemoveItemAsync(item)
    })

OnItemRemoved():
  if (!suppressUndoRecording):
    UndoRedo.Default[this].AddChange("AddItem(...)", () => {
      InsertItemAsync(relativeIndex, parent, item)
    })
```

**Phase 2: Atomic Undo (Explicit)**
```csharp
RemoveSelectedItemsCommand():
  UndoRedo.Default[this].BeginChangeSet("Remove 3 items")
  try:
    isPerformingDelete = true
    base.RemoveSelectedItems()
  finally:
    isPerformingDelete = false
    UndoRedo.Default[this].EndChangeSet()  // Groups all removals

CreateFolderFromSelection():
  suppressUndoRecording = true
  try:
    // Multiple tree operations without individual undo entries
  finally:
    suppressUndoRecording = false

  UndoRedo.Default[this].AddChange("Create Folder (...)", () => {
    // Single atomic undo: restore layout
  })
```

### Safety Patterns

**Defensive Undo Actions**:
```csharp
UndoRedo.Default[this].AddChange("RemoveItem(...)", () => {
  try:
    // Verify item still exists before removal
    if (item.Parent is TreeItemAdapter parent):
      var children = parent.Children.ConfigureAwait(false).GetResult()
      if (!children.Contains(item))
        return  // Item already gone; nothing to do

    RemoveItemAsync(item)
  catch (Exception ex):
    logger.LogWarning(ex, "Undo failed for '{Item}'", item.Label)
    // Swallow exception to keep undo system robust
})
```

**Why**: Undo actions are deferred; item state may change between recording and undo execution. Defensive checks prevent crashes.

---

## Engine Synchronization Pipeline

### Service Integration: ISceneEngineSync

The ViewModel delegates **all engine operations** to `ISceneEngineSync`:

```csharp
private readonly ISceneEngineSync sceneEngineSync;

// Scene load
await sceneEngineSync.SyncSceneAsync(scene)

// Runtime operations
await sceneEngineSync.CreateNodeAsync(entity)
await sceneEngineSync.RemoveNodeAsync(entity.Id)
await sceneEngineSync.ReparentNodeAsync(entity.Id, newParentId)
```

### Why Service Abstraction?

1. **Decoupling**: ViewModel doesn't know engine details
2. **Testing**: Mock service for unit tests
3. **Replacement**: Engine can change without ViewModel changes
4. **Async**: All operations are async (can be expensive)
5. **Error Handling**: ViewModel catches exceptions, logs, continues

### Error Handling Strategy

```csharp
OnItemBeingAdded():
  try:
    entity.SetParent(newParent)
    await sceneEngineSync.ReparentNodeAsync(...)
    messenger.Send(new SceneNodeReparentedMessage(...))
  catch (Exception ex):
    logger.LogError(ex, "Failed to reparent node '{NodeName}'", entity.Name)
    // UI state remains partially consistent
```

**Pattern**: Catch and log, don't crash. UI has already modified adapters; best effort to sync engine.

---

## Layout Persistence and Reconstruction

### Loading Scene with Layout

```csharp
LoadSceneAsync(Scene scene):
  // Create scene adapter as UI root
  Scene = new SceneAdapter(scene) { IsExpanded = true, IsRoot = true }

  // Initialize from ExplorerLayout (or null = fallback to RootNodes)
  await InitializeRootAsync(Scene)
    → LoadChildren()
        if (layout != null && layout.Count > 0):
          BuildFromEntry(entry)  // Recursively build adapters from layout
            - Create FolderAdapters for Folder entries
            - Create SceneNodeAdapters for Node entries
            - Reconstruct hierarchy from ExplorerLayout structure
        else:
          // Flat listing of RootNodes
          for each node in RootNodes:
            Add SceneNodeAdapter

  // Sync engine with entire scene
  await sceneEngineSync.SyncSceneAsync(scene)
```

### Reloading Layout After Undo

```csharp
ReloadChildrenAsync():
  ClearChildren()
  await LoadChildren()  // Reconstructs tree from ExplorerLayout
```

**Why Not Recreate SceneAdapter?**: Reusing the adapter preserves expand/collapse state, selection context, and scroll position.

### Layout Backward Compatibility

```csharp
SceneAdapter.LoadChildren():
  if (layout != null && layout.Count > 0):
    // New path: use layout
  else:
    // Fallback: use flat RootNodes list
    for each node in RootNodes:
      Add SceneNodeAdapter
```

**Result**: Old scenes without layout data still work; they just show flat node list.

---

## Key UX Insights

### 1. **No Cached Selection**

```csharp
[RelayCommand(CanExecute = nameof(CanAddEntity))]
private async Task AddEntity():
  var selectedItem = SelectionModel?.SelectedItem  // Read CURRENT state
  if (selectedItem is null):
    return
```

**Why**: Selection can change between command enable-check and execution. Always read live state.

**Implication**: UI responds to actual current state, not potentially stale cached state.

### 2. **Immediate Visual Feedback**

```csharp
CreateFolderFromSelection():
  // After creating folder:
  if (!folderAdapter.IsExpanded):
    await ExpandItemAsync(folderAdapter)
  ClearAndSelectItem(folderAdapter)  // Select for immediate visibility
```

**Why**: Users should immediately see their action's result.

**Implication**: No wait for server response; UI updates happen first, then engine sync.

### 3. **Selection Locking**

```csharp
[ObservableProperty]
[NotifyCanExecuteChangedFor(RemoveSelectedItemsCommand)]
public partial bool HasUnlockedSelectedItems { get; set; }

OnMultipleSelectionChanged():
  unlockedSelectedItems = selectedIndices.Any(i => !ShownItems[i].IsLocked)
  HasUnlockedSelectedItems = unlockedSelectedItems
```

**Why**: Only allow delete/modify on unlocked items.

**Implication**: Root scene and protected items can't be deleted.

### 4. **Folder Operations Don't Change Runtime**

```csharp
OnItemBeingAdded():
  if (args.Parent is FolderAdapter):
    // Only update ExplorerLayout
    // Do NOT modify scene.RootNodes
    // Do NOT call engine sync with hierarchy change

    // But DO create node if not already created:
    if (!entity.IsActive):
      await sceneEngineSync.CreateNodeAsync(entity)
```

**Why**: Folders are pure UI organization, not engine hierarchy.

**Implication**: Dragging node into folder is cheap (layout update only). Creating new node always syncs engine.

### 5. **Move vs Delete Distinction**

```csharp
RemoveSelectedItemsCommand():
  isPerformingDelete = true

OnItemBeingRemoved():
  if (isPerformingDelete):
    // Remove from scene, sync engine
  else:
    // Just capture old parent for later reparent detection
```

**Why**: Users expect drag = move (no engine message), delete key = remove (engine removed).

**Implication**: Same removal event has different semantics based on command context.

### 6. **Atomic Undo for Complex Operations**

```csharp
CreateFolderFromSelection():
  suppressUndoRecording = true
  // Perform: remove nodes from layout, add folder, move adapters
  suppressUndoRecording = false

  UndoRedo.Default[this].AddChange("Create Folder", () => {
    // Single undo entry: swap layout, reload adapters
  })
```

**Why**: User thinks "create folder" = one action, not 10 tree modifications.

**Implication**: Undo feels natural; user actions map to undo entries.

---

## Message Types and Propagation

### SceneNodeAddedMessage
- **Triggered**: When new SceneNode created or moved under parent
- **Subscribers**: Properties panel, scene initialization, other tools
- **Payload**: Array of nodes added
- **Engine Already Synced**: Yes, CreateNodeAsync called before message

### SceneNodeRemovedMessage
- **Triggered**: Only when actual delete (not move), and only for deleted adapters
- **Subscribers**: Linked components that cache node references
- **Payload**: Array of nodes removed
- **Engine Already Synced**: Yes, RemoveNodeAsync called before message

### SceneNodeReparentedMessage
- **Triggered**: When node moved from one parent to another
- **Subscribers**: Hierarchy-aware tools (gizmo controller, etc.)
- **Payload**: Node + old parent ID + new parent ID
- **Engine Already Synced**: Yes, ReparentNodeAsync called before message

### SceneNodeSelectionChangedMessage
- **Triggered**: Whenever selection changes (single or multiple)
- **Subscribers**: Properties panel, viewport gizmo, other inspectors
- **Payload**: Array of selected scene nodes (folders excluded)
- **Frequency**: High (multiple changes per interaction)

### SceneNodeSelectionRequestMessage
- **Pattern**: Request/Reply (not fire-and-forget)
- **Triggered**: When external component needs current selection
- **Subscribers**: Explorer responds with current SelectionModel state
- **Payload (Request)**: None (implicit query)
- **Payload (Reply)**: Array of currently selected nodes

---

## Performance Considerations

### O(n) Layout Searching
```csharp
FindFolderEntryWithParent(IList<ExplorerEntryData>? entries, Guid id):
  // Recursive search through potentially nested structure
  // Could be O(n*d) where n=entries, d=depth
```

**Mitigation**: Layouts typically shallow (few nesting levels), small n.

### Adapter Tree Reconstruction
```csharp
LoadChildren():
  // For each ExplorerLayout entry, build corresponding adapter
  // If AllNodes traversal needed, could be expensive
```

**Mitigation**: Happens once at scene load, not per-frame.

### Selection Change Messaging
```csharp
OnMultipleSelectionChanged():
  messenger.Send(new SceneNodeSelectionChangedMessage(...))
  // Fire on every index change, could be rapid during multi-select
```

**Mitigation**: Receivers throttle/debounce if needed. Messenger is in-process, fast.

---

## Extension Points and Design Patterns

### Adding New Commands

1. **Add RelayCommand to ViewModel**:
   ```csharp
   [RelayCommand(CanExecute = nameof(CanMyCommand))]
   private async Task MyCommand()
   ```

2. **Implement CanExecute if needed**:
   ```csharp
   private bool CanMyCommand() => SelectionModel?.SelectedItem is SceneNodeAdapter
   ```

3. **Call service methods for engine sync**:
   ```csharp
   await sceneEngineSync.UpdateNodeTransformAsync(node)
   ```

4. **Send message to notify subscribers**:
   ```csharp
   messenger.Send(new CustomMessage(...))
   ```

### Adding New Event Handlers

Intercept tree lifecycle:
```csharp
// Existing: OnItemBeingAdded, OnItemAdded, OnItemBeingRemoved, OnItemRemoved

// Pattern for model mutations: BeingAdded/BeingRemoved
// Pattern for undo recording: Added/Removed
```

### Adding New Adapters

1. Derive from `TreeItemAdapter`
2. Implement `Label` property
3. Implement `LoadChildren()` for lazy loading
4. Optionally implement `ITreeItem<T>` for strong typing

---

## Conclusion: Architectural Strengths

1. **Clear Separation**: UI adapters ≠ model ≠ engine
2. **Event-Driven**: Decoupled components via messaging
3. **Layered**: Each layer has single responsibility
4. **Defensive**: Guard flags, exception handling, safe undo
5. **Explicit**: No magic; control flow is clear
6. **Scalable**: Easy to add commands, handlers, messages
7. **Testable**: Service abstraction allows mocking
8. **User-Friendly**: Atomic undo, immediate feedback, no cached state

The design prioritizes **clarity and maintainability** over raw performance, making it a solid foundation for a professional editor tool.
