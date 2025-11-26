# DroidNet.Documents

> **Document management abstractions for the Oxygen Editor (WinUI/.NET)**

---

## Overview

DroidNet.Documents provides the core interfaces and event contracts for document management in the Oxygen Editor. It enables apps to manage documents, synchronize UI state, and handle tabbed document workflows in WinUI applications.

> [!TIP]
> This module is designed for integration with the Aura UI layer and supports multi-window scenarios, async document closing, and custom metadata.

---

## Features

- **IDocumentService**: Main contract for document lifecycle events (open, close, activate).
- **IDocumentMetadata**: Minimal DTO for document identity, title, icon, and dirty state.
- **IDocumentServiceState**: Query open/active documents per window.
- **Event Args**: Rich event argument types for document operations, including async close veto.

---

## Usage

1. **Implement `IDocumentService`** in your app to manage document state and raise events.
2. **Subscribe** to events in the UI layer (Aura) to update tab strips and window lists.
3. **Use `IDocumentMetadata`** to provide document info for UI rendering.
4. **Support async close** by handling veto tasks in `DocumentClosingEventArgs`.

```csharp
public class MyDocumentService : IDocumentService, IDocumentServiceState
{
 // Implement event raising and state queries
}
```

---

## API Surface

- `IDocumentService`
- `IDocumentMetadata`
- `IDocumentServiceState`
- Event args: `DocumentOpenedEventArgs`, `DocumentClosingEventArgs`, etc.

See [src/](src/) for full interface definitions.

---

## Integration

- **Target Framework**: `net9.0-windows10.0.26100.0`
- **NuGet Packages**: Microsoft.WindowsAppSDK, Microsoft.Windows.SDK.BuildTools
- **Tags**: winui, documents, document-management, editor

---

> [!NOTE]
> DroidNet.Documents is part of the modular DroidNet ecosystem. For UI and routing, see the Aura and Routing modules.

---

## Getting Started

Add a reference to the project and implement the required interfaces in your application. For advanced scenarios, consult the Oxygen Editor and Aura documentation.
