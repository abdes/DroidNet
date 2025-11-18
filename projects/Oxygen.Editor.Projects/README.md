# Oxygen Editor Projects

Core, UI-independent module providing abstractions and utilities for manipulating Oxygen projects, including scene management, node hierarchies, components, and serialization.

## Overview

Oxygen Editor Projects is a foundational library for the Oxygen game engine editor that provides:

- **Scene and node management**: In-memory representation of game scenes, nodes, and component hierarchies
- **Serialization**: JSON-based persistence for projects and scenes with polymorphic component support
- **Project lifecycle**: High-level APIs for creating, loading, and saving Oxygen projects
- **Data abstractions**: Type-safe interfaces for projects, scenes, and components

The module is UI-agnostic and .NET-focused, designed to be consumed by editor UI layers, tools, and other services that need to manipulate Oxygen project data.

## Technology Stack

| Technology | Version | Purpose |
|---|---|---|
| **.NET** | 9.0 (Windows 10.0.26100.0) | Target framework |
| **C#** | 13 (preview) | Language with nullable reference types, ImplicitUsings |
| **System.Linq.Async** | Latest | Async LINQ support |
| **Microsoft.Extensions.Logging** | Latest | Logging abstractions |
| **Microsoft.Extensions.Options** | Latest | Configuration patterns |

## Architecture

The module follows a layered architecture with clear separation of concerns:

```text
┌─────────────────────────────────────┐
│  External Consumers                 │
│  (Editor UI, Tools, Tooling)        │
└────────────────┬────────────────────┘
                 │
    ┌────────────▼────────────┐
    │  ProjectManagerService  │
    │  (Lifecycle API)        │
    └────────────┬────────────┘
                 │
    ┌────────────▼────────────────┐
    │  Core Models & Converters   │
    │  - Scene / SceneNode        │
    │  - Component / Transform    │
    │  - SceneJsonConverter       │
    │  - Category                 │
    └────────────┬────────────────┘
                 │
    ┌────────────▼────────────────┐
    │  Storage & Serialization    │
    │  (Oxygen.Editor.Storage)    │
    └─────────────────────────────┘
```

**Key design principles:**

- **UI-agnostic**: No WinUI or rendering dependencies; strictly data model and serialization
- **Immutable metadata**: Project `Id` (GUID) is required and stable
- **Polymorphic serialization**: Component types are discoverable via `type` property in JSON
- **Async-first**: All I/O operations are async for responsiveness

## Project Structure

```text
src/
  ├── Scene.cs                 # Root scene container
  ├── SceneNode.cs             # Hierarchical node in a scene
  ├── Component.cs             # Base component class
  ├── Transform.cs             # Position, rotation, scale component
  ├── GameComponent.cs         # Game-specific component
  ├── GameObject.cs            # Scene node wrapper
  ├── Category.cs              # Organizational categories
  ├── CategoryJsonConverter.cs # Category serialization
  ├── SceneJsonConverter.cs    # Scene graph serialization
  ├── Project.cs               # Project model
  ├── ProjectInfo.cs           # Project metadata
  ├── ProjectManagerService.cs # Lifecycle management
  ├── IProject.cs              # Project interface
  ├── IProjectInfo.cs          # Project metadata interface
  ├── IProjectManagerService.cs# Service interface
  ├── Constants.cs             # Shared constants
  └── Utils/                   # Utility helpers
  └── Strings/                 # Localization & string resources
  └── Properties/              # Assembly info

tests/
  └── [MSTest unit tests]      # Model and service validation

Resources/
  └── [Shared resources]       # Helpers and resources
```

## Key Features

### Scene and Node Management

- **Hierarchical scenes**: Organize game objects in tree structures with parent-child relationships
- **Node types**: Support for different scene node categories (Actors, Props, UI, etc.)
- **Properties**: Extendable node attributes and metadata

### Component System

- **Transform component**: Position, rotation, scale for 3D/2D placement
- **Game components**: Custom game logic containers
- **Polymorphic serialization**: Component types tracked in JSON for correct deserialization

### Project Lifecycle

- **Create**: Initialize new projects with metadata and initial scenes
- **Load**: Deserialize projects and scenes from JSON with validation
- **Save**: Serialize projects and scenes with proper formatting
- **Dirty tracking**: Monitor project state changes for save prompts

### Serialization & Formats

- **JSON-based**: Human-readable, versionable project and scene files
- **Graph support**: Handles cyclic references and node relationships
- **Validation**: Ensures required fields (e.g., project `Id`) are present

## JSON formats

The library uses JSON for project and scene storage. The formats below reflect
the in-memory model and are intended to be stable, but may evolve. The project
includes `SceneJsonConverter` to handle graph structures and references.

Example: Project file (`.oxygen.project.json` or similar)

```json
{
  "Id": "8f2b2b16-3a4a-4f39-9f2c-2d9f3a1b2c3d",
  "projectName": "MyGame",
  "version": "1.0",
  "scenes": [
    {
      "id": "Scenes/Main.scene.json",
      "displayName": "Main"
    }
  ],
  "metadata": {
    "created": "2025-01-01T00:00:00Z",
    "author": "Developer"
  }
}
```

Note: The `Id` property (capital `I`) is required and must be a non-empty GUID.
The loader will throw a `JsonException` if `Id` is missing or invalid.

Example: Scene file (`Main.scene.json`)

```json
{
  "id": "main-scene",
  "name": "Main",
  "rootNodes": [
    {
      "id": "node-1",
      "name": "Player",
      "category": "Actors",
      "components": [
        {
          "type": "Transform",
          "position": { "x": 0, "y": 1, "z": 0 },
          "rotation": { "x": 0, "y": 0, "z": 0 },
          "scale": { "x": 1, "y": 1, "z": 1 }
        }
      ],
      "children": []
    }
  ]
}
```

Notes:

- Nodes form a tree. Each node has a unique `id` within the scene and may
  reference other nodes or resources via IDs.
- Components are polymorphic objects. The `SceneJsonConverter` serializes
  component `type` information so the runtime can deserialize concrete component
  types.
- Optional fields may be omitted to keep files compact. Readers should treat
  missing values as defaults.

## Serialization & `SceneJsonConverter`

`SceneJsonConverter` handles:

- Polymorphic component types (mapping `type` property to concrete CLR types).
- Graph cycles and preserving node `id` references when necessary.
- Backwards compatibility for some simple schema changes.

When adding new component types, ensure:

- The concrete component type is discoverable by the converter (added to known
  types or registered).
- The JSON schema for the component is stable or versioned.

## ProjectManagerService

`ProjectManagerService` is the primary entry point to manage projects
programmatically. Typical responsibilities:

- Create a new project with an initial scene.
- Load an existing project file and its scenes.
- Save project metadata and scenes to disk.
- Provide change notifications for project state (dirty flags, open scene list).

API usage (conceptual):

```csharp
// Create or open a project
var project = await projectManager.CreateProjectAsync(path, options);

// Open a scene
var scene = await projectManager.LoadSceneAsync(project, "Scenes/Main.scene.json");

// Modify model
var node = new SceneNode { Name = "NewNode" };
scene.RootNodes.Add(node);

// Save
await projectManager.SaveSceneAsync(project, scene);
await projectManager.SaveProjectAsync(project);
```

Refer to `tests/ProjectManagerServiceTests.cs` for concrete usage patterns and
expected behaviors.

## Testing

Unit tests live in `tests/` and exercise serialization, node/scene manipulation
and the project service. Run tests via your preferred test runner for .NET 8
(for example `dotnet test` in the solution or test project folder).

## Packaging

The project produces a NuGet package during CI/build. The package contains the
runtime assemblies and embedded resources required by editor UIs.

## Troubleshooting

- If JSON (de)serialization fails for a component, verify the `type` property
  and that the type is registered with the converter.
- For ambiguous string comparisons in model code, prefer `string.Equals(a, b,
  StringComparison.Ordinal)` to avoid culture/NULL issues.
