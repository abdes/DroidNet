# Oxygen.Editor.Documents

> **Concrete document management for the Oxygen Editor (WinUI/.NET)**

---

## Overview

Oxygen.Editor.Documents implements the document management system for the Oxygen Editor, providing robust abstractions and event-driven APIs for tabbed documents, metadata, and scene layouts. It integrates with Aura for UI and supports multi-window workflows.

> [!TIP]
> This module builds on DroidNet.Documents and adds editor-specific features, including scene document layouts and advanced metadata.

---

## Features

- **EditorDocumentService**: Manages document lifecycle, integrates with Aura tabs, supports logging.
- **BaseDocumentMetadata**: Abstract base for all editor documents, with extensible metadata.
- **SceneDocumentMetadata**: Specialized metadata for scene documents, including viewport layout.
- **SceneViewLayout**: Enum for scene editor viewport configurations.
- **Event-driven API**: Raise and handle document events for open, close, detach, and more.

---

## Usage

1. **Instantiate `EditorDocumentService`** in your editor application.
2. **Extend `BaseDocumentMetadata`** for custom document types.
3. **Use `SceneDocumentMetadata`** for scene tabs with layout configuration.
4. **Subscribe to events** to update UI and manage document state.

```csharp
var docService = new EditorDocumentService(loggerFactory);
docService.DocumentOpened += (s, e) => { /* handle open */ };
```

---

## API Surface

- `EditorDocumentService`
- `BaseDocumentMetadata`
- `SceneDocumentMetadata`
- `SceneViewLayout`
- Event args: `DocumentOpenedEventArgs`, `DocumentClosingEventArgs`, etc.

See [src/](src/) for full implementation details.

---

## Integration

- **Target Framework**: `net9.0-windows10.0.26100.0`
- **NuGet Packages**: Microsoft.WindowsAppSDK, Microsoft.Extensions.Logging.Abstractions
- **Tags**: winui, documents, document-management, editor

---

> [!NOTE]
> This module depends on DroidNet.Documents for core abstractions. For UI, see Aura; for routing, see Routing.

---

## Getting Started

Reference the project in your solution, implement or extend the provided classes, and wire up document events in your editor.

---

For advanced scenarios, see the main [DroidNet README](../../README.md) and design docs in `/plan`.

---
