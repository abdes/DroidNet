# Oxygen.Editor.Documents

## Problem

Multi-window Oxygen editors require sophisticated document management:

- Tracking open documents across multiple windows
- Preventing users from closing critical singleton documents (like the current scene)
- Coordinating document state changes and UI updates
- Handling async document close operations with user vetoes (e.g., "Save before closing?")
- Supporting arbitrary document types with type-safe metadata

**Oxygen.Editor.Documents** solves these problems with a clean, event-driven API built on minimal abstractions.

## Overview

Oxygen.Editor.Documents provides the complete document lifecycle management system for the Oxygen Editor. It integrates seamlessly with Aura's tab UI, supports per-window document management, and enforces architectural constraints (like "only one scene per window") through a declarative, type-safe model.

Key responsibilities:

- **Lifecycle Management**: Open, close, attach, detach, and select documents
- **Type Safety**: Custom metadata classes ensure compile-time safety for document properties
- **Singleton Enforcement**: Automatically ensures only one non-closable document of each type per window
- **Async Coordination**: Coordinate document close operations with async save dialogs
- **Event-Driven**: Rich event system for UI state synchronization and business logic hooks

## Core Concepts

### Document Metadata

The core `IDocumentMetadata` contract and the minimal document service interfaces live in the shared module: [DroidNet.Documents/README.md](../Documents/README.md).

For editor-specific metadata, extend `BaseDocumentMetadata` (provided by this repository) and implement a unique `DocumentType` string to enable type-based singleton enforcement.

Example pattern (in your editor project):

```csharp
public class MyCustomDocumentMetadata : BaseDocumentMetadata
{
    public override string DocumentType => "MyCustomType";
    public SomeCustomProperty MyProperty { get; set; }
}
```

### Singleton Documents

Documents that must exist as a single instance per window ("singletons") should be enforced at the metadata *type* level by implementing `IsClosable` to always return `false` for that type. That makes the singleton intent explicit and prevents callers from accidentally creating closable instances.

Example — set `IsClosable` in the document metadata constructor (works regardless of whether the base property is virtual):

```csharp
public class SceneDocumentMetadata : BaseDocumentMetadata
{
    public SceneDocumentMetadata(Guid? documentId = null) : base(documentId)
    {
        // Make this document type non-closable by default (singleton)
        this.IsClosable = false;
    }

    public override string DocumentType => "Scene";
    public SceneViewLayout ViewportLayout { get; set; } = SceneViewLayout.OnePane;
}
```

If `BaseDocumentMetadata.IsClosable` becomes `virtual` in the future, you can instead override the property to always return `false`.

When opening a new singleton document, the service will attempt to close any existing singleton of the same type first (subject to veto by `DocumentClosing` handlers).

### Document Matching

Singleton enforcement uses **type-based matching**:

- For `BaseDocumentMetadata` subclasses: matches by `DocumentType` string
- For other `IDocumentMetadata` implementations: matches by C# type

This allows multiple independent singleton types in the same window:

```csharp
// Both can coexist in the same window:
var scene = new SceneDocumentMetadata { IsClosable = false };
var properties = new PropertiesWindowMetadata { IsClosable = false };

await docService.OpenDocumentAsync(windowId, scene);      // OK
await docService.OpenDocumentAsync(windowId, properties); // OK (different type)
```

### Event-Driven Architecture

The service raises events at each document lifecycle stage, enabling UI updates, business logic, and async workflows:

```csharp
// Document opened → Update UI, initialize editor view
docService.DocumentOpened += (s, e) =>
{
    viewModel.AddEditorTab(e.Metadata);
};

// Document closing → Offer save dialog, allow user to veto
docService.DocumentClosing += (s, args) =>
{
    if (args.Metadata.IsDirty && !args.Force)
    {
        args.AddVetoTask(
            ShowSaveDialogAsync(args.Metadata) // ← Async veto with user decision
        );
    }
};

// Document closed → Clean up resources
docService.DocumentClosed += (s, e) =>
{
    viewModel.RemoveEditorTab(e.Metadata.DocumentId);
};
```

## Usage Guide

### 1. Set Up the Service

```csharp
var loggerFactory = services.GetRequiredService<ILoggerFactory>();
var docService = new EditorDocumentService(loggerFactory);

// Wire up event handlers
docService.DocumentOpened += OnDocumentOpened;
docService.DocumentClosed += OnDocumentClosed;
docService.DocumentClosing += OnDocumentClosing;
```

### 2. Define Custom Document Types

```csharp
public class SceneDocumentMetadata : BaseDocumentMetadata
{
    public override string DocumentType => "Scene";
    public SceneViewLayout ViewportLayout { get; set; } = SceneViewLayout.OnePane;
}

public class PropertiesDocumentMetadata : BaseDocumentMetadata
{
    public override string DocumentType => "Properties";
}
```

### 3. Open Documents

```csharp
// Regular closable document
var metadata = new SceneDocumentMetadata(sceneId)
{
    Title = "Scene: MyScene.scene",
    IsClosable = true,
};
var docId = await docService.OpenDocumentAsync(windowId, metadata, shouldSelect: true);

// Singleton document (only one per window)
var propsMeta = new PropertiesDocumentMetadata
{
    Title = "Properties",
    IsClosable = false,  // Singleton enforcement
};
await docService.OpenDocumentAsync(windowId, propsMeta);
```

### 4. Handle Close Events with Veto

```csharp
private void OnDocumentClosing(object? sender, DocumentClosingEventArgs e)
{
    var meta = e.Metadata;

    // If the document is dirty and close isn't forced, ask the user
    if (meta.IsDirty && !e.Force)
    {
        // Add an async veto task that returns false if user cancels
        e.AddVetoTask(PromptSaveAsync(meta));
    }
}

private async Task<bool> PromptSaveAsync(IDocumentMetadata metadata)
{
    var result = await new SaveChangesDialog(metadata.Title).ShowAsync();

    if (result == ContentDialogResult.Primary)
    {
        // Save
        await SaveDocumentAsync(metadata.DocumentId);
        return true; // Proceed with close
    }
    else if (result == ContentDialogResult.Secondary)
    {
        // Don't save
        return true; // Proceed with close
    }
    else
    {
        // Cancel
        return false; // Veto close
    }
}
```

### 5. Select and Activate Documents

```csharp
// Make a document the active tab
bool selected = await docService.SelectDocumentAsync(windowId, documentId);
if (!selected)
{
    // Document not found
}
```

### 6. Close Documents

```csharp
// Close with veto support (prompts user, respects veto)
bool closed = await docService.CloseDocumentAsync(windowId, documentId, force: false);
if (!closed)
{
    // User vetoed the close
}

// Force close (bypasses veto, useful for app shutdown)
await docService.CloseDocumentAsync(windowId, documentId, force: true);
```

## Event Model

| Event | When Raised | Async? | Veto Support |
| ----- | ----------- | ------ | ------------ |
| `DocumentOpened` | Document successfully opened | No | — |
| `DocumentClosing` | Before document close (if not forced) | Yes | ✓ Add veto tasks |
| `DocumentClosed` | After document successfully closed | No | — |
| `DocumentActivated` | Document selected/activated | No | — |
| `DocumentDetached` | Document detached from service | No | — |
| `DocumentAttached` | Document attached to service | No | — |
| `DocumentMetadataChanged` | Document metadata updated | No | — |

### Veto Pattern

Use `DocumentClosing` to implement async user confirmation:

```csharp
docService.DocumentClosing += (s, args) =>
{
    args.AddVetoTask(async () =>
    {
        if (userChoosesToCancel)
            return false; // Veto → close is cancelled
        return true; // Allow → close proceeds
    });
};
```

Multiple handlers can add veto tasks; if any returns `false`, the close is vetoed.

## Integration Points

### Aura Document Tabs

`EditorDocumentService` integrates with Aura's tab system:

- Raise `DocumentOpened` → Aura adds a tab
- Raise `DocumentClosed` → Aura removes the tab
- Subscribe to tab close events → Call `CloseDocumentAsync`

### ViewModels and Views

Use events to keep ViewModels and Aura views in sync:

```csharp
docService.DocumentOpened += (s, e) =>
{
    var editor = CreateEditorViewModel(e.Metadata);
    viewModel.Editors[e.Metadata.DocumentId] = editor;
};

docService.DocumentActivated += (s, e) =>
{
    viewModel.ActiveEditor = viewModel.Editors[e.DocumentId];
};
```

## Advanced Scenarios

### Multi-Type Singleton Management

Multiple singleton types in one window:

```csharp
// Scene documents (singleton)
await docService.OpenDocumentAsync(windowId,
    new SceneDocumentMetadata(sceneId) { IsClosable = false });

// Properties panel (singleton, different type)
await docService.OpenDocumentAsync(windowId,
    new PropertiesDocumentMetadata { IsClosable = false });

// Both remain open—they're different types
```

### Document Attach/Detach

Temporarily remove a document without destroying it:

```csharp
// Remove from service (no events raised)
var metadata = await docService.DetachDocumentAsync(windowId, docId);

// ... Do something with metadata ...

// Re-attach to the same or different window
await docService.AttachDocumentAsync(targetWindowId, metadata);
```

### Updating Metadata

Modify and persist document metadata changes:

```csharp
metadata.Title = "Updated Title";
metadata.IsDirty = true;

await docService.UpdateMetadataAsync(windowId, documentId, metadata);
// Raises DocumentMetadataChanged
```

## Dependencies

- **DroidNet.Documents**: Core `IDocumentService` interface and event args
- **Microsoft.Extensions.Logging**: Structured logging

## See Also

- [DroidNet.Documents](../Documents/README.md) — Core abstractions
- [Aura](../Aura/README.md) — Tab UI integration
- [Oxygen.Editor.World](../Oxygen.Editor.World/) — Example usage: scene documents
