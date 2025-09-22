# Oxygen Editor Projects

This module contains the core, UI-independent definitions and logic for Oxygen projects: scenes, nodes, components, categories and project management. It is intended to be consumed by editor UI layers and other tooling that need to load, save and manipulate Oxygen project data.

## Projects overview

An Oxygen project groups one or more scenes and related metadata. This project library provides types and services to:

- Represent scenes, nodes and components in memory.
- Serialize/deserialize projects and scenes to/from JSON.
- Manage project lifecycle (create, open, save) via `ProjectManagerService`.
- Provide tests and helpers to validate model behavior.

The project targets .NET 8 and is UI-agnostic: rendering and editor UI are implemented elsewhere.

## Repository structure

- `src/` - Core types and services:
  - `Scene.cs`, `SceneNode.cs`, `Component` and concrete components such as `Transform.cs`, `GameComponent.cs`.
  - `Category.cs` - organizational categories used by the editor.
  - `ProjectManagerService.cs` - high-level project management API (load/save/create projects and scenes).
  - `SceneJsonConverter.cs` - custom JSON converter used for (de)serializing scene graphs.

- `tests/` - Unit tests for the core model and services.

- `Resources/` - shared resources and helpers used by the project packages.


## JSON formats

The library uses JSON for project and scene storage. The formats below reflect the in-memory model and are intended to be stable, but may evolve. The project includes `SceneJsonConverter` to handle graph structures and references.

Example: Project file (`.oxygen.project.json` or similar)

```json
{
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
- Nodes form a tree. Each node has a unique `id` within the scene and may reference other nodes or resources via IDs.
- Components are polymorphic objects. The `SceneJsonConverter` serializes component `type` information so the runtime can deserialize concrete component types.
- Optional fields may be omitted to keep files compact. Readers should treat missing values as defaults.

## Serialization & `SceneJsonConverter`

`SceneJsonConverter` handles:
- Polymorphic component types (mapping `type` property to concrete CLR types).
- Graph cycles and preserving node `id` references when necessary.
- Backwards compatibility for some simple schema changes.

When adding new component types, ensure:
- The concrete component type is discoverable by the converter (added to known types or registered).
- The JSON schema for the component is stable or versioned.

## ProjectManagerService

`ProjectManagerService` is the primary entry point to manage projects programmatically. Typical responsibilities:

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

Refer to `tests/ProjectManagerServiceTests.cs` for concrete usage patterns and expected behaviors.

## Testing

Unit tests live in `tests/` and exercise serialization, node/scene manipulation and the project service. Run tests via your preferred test runner for .NET 8 (for example `dotnet test` in the solution or test project folder).

## Packaging

The project produces a NuGet package during CI/build. The package contains the runtime assemblies and embedded resources required by editor UIs.

## Troubleshooting

- If JSON (de)serialization fails for a component, verify the `type` property and that the type is registered with the converter.
- For ambiguous string comparisons in model code, prefer `string.Equals(a, b, StringComparison.Ordinal)` to avoid culture/NULL issues.
