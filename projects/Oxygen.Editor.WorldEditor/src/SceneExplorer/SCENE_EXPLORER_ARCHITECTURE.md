# Scene Explorer Architecture

## 1. Overview

The **Scene Explorer** is the primary interface for visualizing and manipulating the hierarchy of a Scene in the Oxygen Editor. It is not just a simple tree view of the data; it is a complex system that bridges the gap between the **Runtime Scene Graph** (physics, rendering) and the **Editor Layout** (user organization, folders).

### Key Responsibilities

* **Visualization**: Displaying the scene hierarchy.
* **Organization**: Allowing users to group nodes into virtual "Folders" that do not exist in the engine.
* **Manipulation**: Handling drag-and-drop, parenting, and reordering.
* **Synchronization**: Ensuring changes in the editor are reflected in the native game engine.

---

## 2. Core Concepts: The "Two Trees" Problem

The central architectural challenge of the Scene Explorer is maintaining two distinct tree structures that must stay in sync but serve different purposes.

### A. The Scene Graph (The Truth)

* **Definition**: The runtime hierarchy of `SceneNode` objects.
* **Purpose**: Calculates transforms (position, rotation), physics, and rendering.
* **Invariant**: If Node B is a child of Node A, Node B moves when Node A moves. This is a physical relationship.
* **Storage**: Serialized as the `RootNodes` collection in `SceneData`.

### B. The Explorer Layout (The Overlay)

* **Definition**: A tree of **Adapters** (`SceneNodeAdapter` and `FolderAdapter`) used *only* for the UI.
* **Purpose**: Allows users to group nodes into Folders for organization without altering the physical parent-child relationships.
* **Invariant**: This is a *projection* of the Scene Graph. It can add "virtual" containers (Folders), but it cannot contradict the ancestry defined in the Scene Graph.
* **Storage**: Serialized as `ExplorerLayout` in `SceneData`.

### The "Strict Lineage" Rule

**You cannot move a Node into a Folder if that Folder belongs to a stranger.**

If `Gun` is a child of `Player` in the Scene Graph:

* ✅ **Valid**: `Player (Adapter) -> Weapons (Folder) -> Gun (Adapter)`
* ❌ **Invalid**: `Root (Folder) -> Gun (Adapter)`

**Why?** If the UI shows `Gun` at the root, but it physically moves when `Player` moves, the user will perceive the software as broken. The Layout Tree must always be a valid subset of the Scene Graph.

---

## 3. Component Architecture

The module is divided into distinct layers to separate concerns:

### A. ViewModels (The UI Layer)

* **`SceneExplorerViewModel`**: The brain of the UI.
  * Manages the `ObservableCollection` of `SceneAdapter` items.
  * Handles selection state (`SelectionModel`).
  * Orchestrates high-level operations (Load Scene, Drag & Drop).
  * **Crucial**: Listens for `DocumentOpened` events to load scenes.
* **`SceneAdapter`**: The root of the tree. Can switch between "Layout Mode" (showing folders) and "Raw Mode" (showing only nodes).
* **`SceneNodeAdapter`**: Wraps a `SceneNode`. Bridges the UI item to the real data object.
* **`FolderAdapter`**: A virtual container. Has no attached object. Exists only in the `ExplorerLayout`.

### B. Services (The Logic Layer)

* **`SceneExplorerService`**: The public API for the module.
  * Entry point for operations like `MoveItemAsync`, `CreateNodeAsync`.
  * Coordinating the `SceneMutator`, `SceneOrganizer`, and `SceneEngineSync`.
* **`SceneOrganizer`**: The "Librarian".
  * Manages the `ExplorerLayout` data structure.
  * Knows how to find folders, move nodes into folders, and validate lineage.
  * **Pure Logic**: Does not touch the engine or the `SceneNode` parentage.
* **`SceneMutator`**: The "Surgeon".
  * Manages the `SceneNode` hierarchy.
  * Performs the actual parenting/unparenting of nodes (`node.SetParent(...)`).
  * Enforces scene graph invariants (e.g., no cycles).
* **`SceneEngineSync`**: The "Translator".
  * Talks to the C++ Native Engine via `IEngineService`.
  * Replicates changes made in C# (Create Node, Move Node) to the native world.

---

## 4. Data Flow Examples

### Scenario: Moving a Node into a Folder

1. **User Action**: Drags "Gun" into "Weapons" folder.
2. **ViewModel**: Calls `SceneExplorerService.MoveItemAsync(gunAdapter, weaponsFolderAdapter)`.
3. **Service**:
    * Identifies the target scene.
    * **Validation**: Checks if "Weapons" folder is inside the lineage of "Gun"'s physical parent.
    * **Organizer**: Calls `SceneOrganizer.MoveNodeToFolder`. Updates the `ExplorerLayout` data.
    * **Mutator**: (If necessary) Updates physical parent. In this case, usually no change if just organizing.
    * **Sync**: (If physical parent changed) Calls `SceneEngineSync` to update engine.
4. **ViewModel**: Receives updated layout/change record and refreshes the tree.

### Scenario: Loading a Scene

1. **Trigger**: `DocumentOpened` event fires.
2. **ViewModel**: `OnDocumentOpened` -> `LoadSceneAsync`.
3. **ProjectManager**: Deserializes the JSON into a **NEW** `Scene` object graph.
4. **ViewModel**:
    * **Cancels** any pending load (`CancellationTokenSource`).
    * **Replaces** `this.Scene` with the new object.
    * **Rebuilds** the Adapter Tree (`SceneAdapter.BuildLayoutTree`).
    * **Syncs** the new scene to the Engine (`SceneEngineSync.SyncSceneAsync`).

---

## 5. Critical Pitfalls (Read Before Touching Code)

### 1. Object Identity & The "Stale Scene" Bug

**The Problem:**
When a Scene is re-loaded, the deserializer creates **NEW** `Scene` and `SceneNode` instances. It does *not* update the existing objects in place.

**The Danger:**
If the UI (ViewModel) or a Service (`SceneOrganizer`) holds a reference to the **OLD** `Scene` object, and you try to perform an operation using a **NEW** `SceneNode` (from the new scene), you will crash.

* `node.Scene` will point to `Scene_Instance_B`.
* `organizer.Scene` will point to `Scene_Instance_A`.
* Result: `InvalidOperationException: Folder not found`.

**The Fix:**

* Always ensure that when a Scene is loaded, **ALL** references to the old scene are dropped.
* The `SceneExplorerViewModel` must rebuild its entire tree from the new `Scene` object.
* Services like `SceneExplorerService` must be stateless regarding the Scene, or explicitly refreshed.
* **Never** assume `node.Scene` is the same as `this.Scene`. Always check.

### 2. The "Double Load" Race Condition

**The Problem:**
If `LoadSceneAsync` is called twice rapidly (e.g., once by startup logic, once by navigation logic), you can end up with a race condition.

* Load A starts.
* Load B starts.
* Load B finishes and sets `ViewModel.Scene = Scene_B`.
* Load A's async UI generation finishes and tries to add nodes from `Scene_A` to the tree.

**The Fix:**

* Use `CancellationTokenSource` in `LoadSceneAsync`.
* Cancel any pending load before starting a new one.
* Check `token.IsCancellationRequested` at every `await` boundary.

---

## 6. Persistence

* **Scene Data**: Saved in `.scene` files (JSON).
* **Layout Data**: Saved *inside* the `.scene` file as the `ExplorerLayout` property.
* **Serialization**: Handled by `SceneSerializer`. It serializes both the physical `RootNodes` and the virtual `ExplorerLayout`.

## 7. Future Improvements

* **Selection Sync**: Currently, selection is handled in the ViewModel. It should ideally be synced with a global `SelectionService` to coordinate with the 3D Viewport.
* **Undo/Redo**: Basic support exists, but complex operations involving folder moves need robust testing.
