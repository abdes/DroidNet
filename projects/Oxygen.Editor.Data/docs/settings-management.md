# Settings Management Design Document

## Table of Contents

1. [Overview](#overview)
2. [Core Concepts](#core-concepts)
3. [Type-Safe Setting Keys](#type-safe-setting-keys)
4. [Scoped Settings](#scoped-settings)
5. [Setting Descriptors](#setting-descriptors)
6. [Two Complementary APIs](#two-complementary-apis-property-based-vs-manager-centric)
7. [API Usage Patterns](#api-usage-patterns)
8. [Validation](#validation)
9. [Change Notifications](#change-notifications)
10. [Batch Operations](#batch-operations)
11. [Discovery and Introspection](#discovery-and-introspection)
12. [Performance and Caching](#performance-and-caching)
13. [Integration Guide](#integration-guide)

---

## Overview

The settings management system in Oxygen.Editor.Data provides a **type-safe, scope-aware, validated approach** to application configuration and persistence. It replaces the earlier string-based, unvalidated settings model with compile-time type safety, hierarchical scopes (application-wide and per-project), and automatic validation powered by Data Annotations.

### Design Goals

- **Type Safety**: Strongly-typed keys and values enforce correctness at compile time, eliminating class casts and string-based lookups.
- **Hierarchy**: Support both application-level defaults and per-project overrides with deterministic resolution.
- **Validation**: Declarative validators using standard .NET Data Annotations, enforced before persistence.
- **Discoverability**: Enable UI tools, documentation generators, and diagnostics to introspect settings without runtime reflection.
- **Performance**: In-memory caching and efficient database queries minimize I/O overhead.
- **Atomicity**: Batch operations ensure related settings are saved together or rolled back as a unit.

### Non-Goals

- Automatic persistence of plain properties (the source generator handles that separately).
- Distributed or network-based settings synchronization.
- Complex transformation pipelines or computed settings.

---

## Core Concepts

### Settings Module

A **settings module** is a logical grouping of related configuration values. Each setting belongs to exactly one module. Module names are typically reverse-DNS style (e.g., `"Oxygen.Editor.Docking"`, `"Oxygen.Editor.ProjectBrowser"`) to avoid collisions across the application.

### Settings as Key-Value Pairs

All settings are stored as key-value pairs where:

- **Key** = `(Module, Name)` — uniquely identifies a setting within its module.
- **Value** = arbitrary object serialized to JSON, deserialized on retrieval.
- **Scope** = Application or Project (see [Scoped Settings](#scoped-settings)).

### Type-Safety at the API Layer

The database stores settings as JSON strings, but the **API enforces type safety** through generic types. Only the declared type can be used to retrieve or save a setting.

```csharp
// Compile-time guarantee: Point key can only store/retrieve Points
SettingKey<Point> windowPos = new("Docking", "WindowPosition");
Point value = await manager.LoadSettingAsync(windowPos);  // ✅ Point
await manager.SaveSettingAsync(windowPos, new Point(100, 100));  // ✅ Point

// These would fail compilation:
// int x = await manager.LoadSettingAsync(windowPos);  // ❌ Type mismatch
// await manager.SaveSettingAsync(windowPos, 42);      // ❌ Type mismatch
```

---

## Type-Safe Setting Keys

### SettingKey\<T\>

A `SettingKey<T>` is a typed key that identifies a single setting and constrains its value type.

```csharp
public readonly record struct SettingKey<T>
{
    public string SettingsModule { get; }
    public string Name { get; }

    public string ToPath() => $"{SettingsModule}/{Name}";
}
```

**Creating Keys:**

```csharp
var windowPosKey = new SettingKey<Point>("Docking", "WindowPosition");
var themeKey = new SettingKey<string>("UI", "Theme");
var opacityKey = new SettingKey<double>("Effects", "WindowOpacity");
```

**Immutability:**

Keys are `readonly record struct` types, making them immutable value objects suitable for dictionary keys, method parameters, and cache identifiers.

**Path Format:**

The `ToPath()` method produces a canonical string representation (`"Module/Name"`) used for diagnostics and storage queries.

---

## Scoped Settings

### Scope Hierarchy

Settings can exist at two scope levels:

- **Application** (`SettingScope.Application`) — Global defaults, applies to all projects.
- **Project** (`SettingScope.Project`) — Project-specific overrides, identified by project path.

### SettingContext

The `SettingContext` record specifies where (scope) a setting operation targets:

```csharp
public sealed record SettingContext(SettingScope Scope, string? ScopeId = null)
{
    public static SettingContext Application() => new(SettingScope.Application);
    public static SettingContext Project(string projectPath) => new(SettingScope.Project, projectPath);
}
```

**Usage:**

```csharp
// Save application-wide default
await manager.SaveSettingAsync(
    new SettingKey<string>("UI", "Theme"),
    "Dark",
    SettingContext.Application()
);

// Override for a specific project
await manager.SaveSettingAsync(
    new SettingKey<string>("UI", "Theme"),
    "HighContrast",
    SettingContext.Project(@"C:\projects\MyGame")
);
```

### Resolution Strategy

When loading or querying a setting, the resolution order respects hierarchy:

1. **Project-scoped** — If a project context is provided and a project-scoped value exists, use it.
2. **Application-scoped** — Otherwise, fall back to the application-level value.
3. **Null** — If neither exists, return `null` or the supplied default.

This enables users to set application defaults that can be selectively overridden per project without duplication.

---

## Setting Descriptors

### SettingDescriptor\<T\>

A descriptor holds metadata about a single setting: its key, display information, validation rules, and categorization.

```csharp
public sealed class SettingDescriptor<T> : ISettingDescriptor
{
    public required SettingKey<T> Key { get; init; }
    public string? DisplayName { get; init; }
    public string? Description { get; init; }
    public string? Category { get; init; }
    public IReadOnlyList<ValidationAttribute> Validators { get; init; } = [];
}
```

### Defining Descriptor Sets

Create static descriptor collections by inheriting from `SettingsDescriptorSet` and using the `CreateDescriptor<T>` helper:

```csharp
public sealed class DockingSettingsDescriptors : SettingsDescriptorSet
{
    public static readonly SettingDescriptor<Point> WindowPosition = CreateDescriptor<Point>("Docking", "WindowPosition");

    public static readonly SettingDescriptor<Size> WindowSize = CreateDescriptor<Size>("Docking", "WindowSize");

    [Display(Name = "Window Opacity", Description = "Transparency level (0.0 = transparent, 1.0 = opaque)")]
    [Range(0.0, 1.0, ErrorMessage = "Opacity must be between 0.0 and 1.0")]
    public static readonly SettingDescriptor<double> WindowOpacity = CreateDescriptor<double>("Docking", "WindowOpacity");
}
```

The `CreateDescriptor<T>` helper uses reflection and caller-member-name inference to locate `[Display]`, `[Category]`, and validation attributes on the declaring property.

### Attributes

**Display Attribute:**

```csharp
[Display(Name = "...", Description = "...")]
public static readonly SettingDescriptor<T> Setting = CreateDescriptor<T>(...);
```

Maps to `descriptor.DisplayName` and `descriptor.Description`.

**Category Attribute:**

```csharp
[Category("Layout")]
public static readonly SettingDescriptor<T> Setting = CreateDescriptor<T>(...);
```

Used by UI tools to organize settings.

**Validation Attributes:**

```csharp
[Range(0.0, 1.0)]
[StringLength(100, MinimumLength = 5)]
[Required]
public static readonly SettingDescriptor<string> Setting = CreateDescriptor<T>(...);
```

Applied automatically during save operations and displayed to users.

---

## Two Complementary APIs: Property-Based vs. Manager-Centric

The settings system supports two usage patterns that complement each other for different scenarios.

### Pattern 1: Property-Based (POCO Settings Objects)

Work with strongly-typed settings objects as regular .NET classes:

```csharp
public sealed partial class DockingSettings : ModuleSettings
{
    private Point windowPosition = new(100, 100);
    private double opacity = 1.0;

    public Point WindowPosition
    {
        get => this.windowPosition;
        set => this.SetProperty(ref this.windowPosition, value);  // Auto-validates, tracks modification
    }

    public double WindowOpacity
    {
        get => this.opacity;
        set => this.SetProperty(ref this.opacity, value);
    }

    public DockingSettings() : base("Docking") { }
}

// Usage: Natural and familiar
var settings = new DockingSettings();
await settings.LoadAsync(manager);

settings.WindowPosition = new Point(200, 300);  // Property validation, marks dirty
settings.WindowOpacity = 0.95;

await settings.SaveAsync(manager);  // Batch-saves all modified properties
```

**Advantages:**

- Familiar POCO pattern; works seamlessly with WinUI/XAML binding
- Automatic modification tracking (`IsDirty`, `modifiedProperties`)
- Property validation at assignment time via `ValidationAttribute`
- Change events via `PropertyChanged` without observables
- Built-in `IsLoaded` and `IsDirty` state tracking

**When to Use:**

- UI ViewModels or state management that bind to properties
- Code that owns a single settings instance
- Scenarios where MVVM patterns dominate

### Pattern 2: Manager-Centric API (Direct Key-Value)

Use the manager directly for cross-cutting operations:

```csharp
var batch = manager.BeginBatch();
batch.QueuePropertyChange(DockingSettingsDescriptors.WindowPosition, new Point(100, 200));
batch.QueuePropertyChange(DockingSettingsDescriptors.WindowOpacity, 0.95);
await batch.DisposeAsync();
```

**Advantages:**

- **Cross-module atomicity** — Combine settings from different modules in one transaction
- **Explicit scope control** — Save to project-specific or application-wide scopes directly
- **No object instances** — Useful for utilities, import/export, diagnostics, and tools
- **Validation before persistence** — All items validate before any database writes

**When to Use:**

- Saving settings across multiple modules atomically
- Project-specific settings overrides (scope-aware operations)
- Tools, import/export, settings migration, and diagnostics
- Direct key-value operations without object instantiation

### Integration: Automatic Batch Queueing

When a settings object's properties are modified **inside a batch context**, changes automatically queue to the batch instead of saving immediately:

```csharp
var batch = manager.BeginBatch();

// These property assignments automatically queue to the batch:
settings.WindowPosition = new Point(100, 100);
settings.WindowOpacity = 0.95;

// Equivalent to:
// batch.QueuePropertyChange(DockingSettingsDescriptors.WindowPosition, ...);
// batch.QueuePropertyChange(DockingSettingsDescriptors.WindowOpacity, ...);

await batch.DisposeAsync();  // Single atomic transaction
```

This means both approaches achieve the same atomic semantics when needed:

```csharp
// Approach 1: Via settings object (inside batch)
using var batch = manager.BeginBatch(SettingContext.Application());
settings.WindowPosition = new Point(100, 100);
settings.WindowOpacity = 0.95;
await batch.DisposeAsync();

// Approach 2: Direct manager API (explicit)
using var batch = manager.BeginBatch(SettingContext.Application());
batch.QueuePropertyChange(descriptor1, new Point(100, 100));
batch.QueuePropertyChange(descriptor2, 0.95);
await batch.DisposeAsync();

// Both result in identical behavior: all-or-nothing transaction with validation
```

---

## API Usage Patterns

### Save a Single Setting

**Without descriptor (no validation):**

```csharp
var key = new SettingKey<Point>("Docking", "WindowPosition");
await manager.SaveSettingAsync(key, new Point(100, 200));
```

**With descriptor (includes validation):**

```csharp
var value = new Point(100, 200);
await manager.SaveSettingAsync(DockingSettingsDescriptors.WindowPosition, value);
```

If validation fails, a `SettingsValidationException` is thrown with details about all validation errors.

### Load a Single Setting

**Nullable result:**

```csharp
var key = new SettingKey<Point>("Docking", "WindowPosition");
Point? position = await manager.LoadSettingAsync(key);
```

**With default fallback:**

```csharp
var key = new SettingKey<Point>("Docking", "WindowPosition");
Point position = await manager.LoadSettingAsync(
    key,
    defaultValue: new Point(100, 100)
);
```

**From a specific scope:**

```csharp
// Load project-scoped override if it exists
Point? projectOverride = await manager.LoadSettingAsync(
    key,
    SettingContext.Project(@"C:\projects\MyGame")
);
```

### Query Defined Scopes

Determine which scopes have values for a particular setting:

```csharp
var key = new SettingKey<Point>("Docking", "WindowPosition");
var scopes = await manager.GetDefinedScopesAsync(key, projectId: @"C:\projects\MyGame");
// Result: [SettingScope.Application, SettingScope.Project]
// Indicates the setting is defined at both scopes
```

### Load All Values (Diagnostics)

Retrieve all persisted values for a key across all scopes:

```csharp
var results = await manager.GetAllValuesAsync("Docking/WindowPosition");
// Returns: [(Scope.Application, null, Point(100, 100)),
//           (Scope.Project, "C:\projects\MyGame", Point(200, 200))]
```

Typed variant:

```csharp
var results = await manager.GetAllValuesAsync<Point>("Docking/WindowPosition");
// Returns: [(Scope.Application, null, Point(100, 100)), ...]
```

---

## Validation

### Declarative Validation

Validation rules are declared using standard .NET `[ValidationAttribute]` types:

```csharp
[Range(0.0, 1.0, ErrorMessage = "Opacity must be between 0 and 1")]
[Required(ErrorMessage = "Window position is required")]
public static readonly SettingDescriptor<Point> WindowPosition = CreateDescriptor<Point>(...);
```

### Validation Flow

When saving with a descriptor:

1. Extract validators from the descriptor.
2. Create a `ValidationContext` for each validator.
3. Call `validator.GetValidationResult(value, context)`.
4. Aggregate all failures into a `SettingsValidationException`.

```csharp
try
{
    await manager.SaveSettingAsync(descriptor, invalidValue);
}
catch (SettingsValidationException ex)
{
    foreach (var result in ex.Results)
    {
        Console.WriteLine($"Error: {result.ErrorMessage}");
    }
}
```

### Batch Validation

Batch operations validate all queued items before any persist:

```csharp
var batch = manager.BeginBatch(SettingContext.Application());
batch.QueuePropertyChange(descriptor1, value1);
batch.QueuePropertyChange(descriptor2, invalidValue);

try
{
    await batch.DisposeAsync();  // Throws if any item is invalid
}
catch (SettingsValidationException ex)
{
    // All changes rolled back, database unchanged
}
```

---

## Change Notifications

### Reactive Change Events

Settings changes are published as typed events via `IObservable<SettingChangedEvent<T>>`:

```csharp
public readonly record struct SettingChangedEvent<T>
{
    public required SettingKey<T> Key { get; init; }
    public T? OldValue { get; init; }
    public T? NewValue { get; init; }
    public SettingScope Scope { get; init; }
    public string? ScopeId { get; init; }
}
```

### Subscribing to Changes

```csharp
var key = new SettingKey<Point>("Docking", "WindowPosition");

var subscription = manager.WhenSettingChanged(key)
    .Subscribe(evt =>
    {
        Console.WriteLine($"Position changed from {evt.OldValue} to {evt.NewValue}");
    });
```

### Composing with Rx Operators

The observable interface enables rich composition:

```csharp
manager.WhenSettingChanged(opacityKey)
    .Throttle(TimeSpan.FromMilliseconds(500))
    .Select(evt => evt.NewValue)
    .Where(opacity => opacity > 0.5)
    .Subscribe(opacity => UpdateWindowTransparency(opacity));
```

### Unsubscribing

Dispose the subscription to stop receiving notifications:

```csharp
subscription.Dispose();
```

---

## Batch Operations

### Transactional Batches

Multiple related setting changes can be committed atomically using the batch API:

```csharp
var batch = manager.BeginBatch(SettingContext.Application());

batch
    .QueuePropertyChange(DockingSettingsDescriptors.WindowPosition, new Point(100, 200))
    .QueuePropertyChange(DockingSettingsDescriptors.WindowSize, new Size(1920, 1080))
    .QueuePropertyChange(DockingSettingsDescriptors.WindowOpacity, 0.95);

await batch.DisposeAsync();
```

### All-or-Nothing Semantics

If any queued change fails validation, **all changes are rolled back** and an exception is thrown:

```csharp
var batch = manager.BeginBatch(SettingContext.Application());

batch.QueuePropertyChange(descriptor1, validValue);
batch.QueuePropertyChange(descriptor2, invalidValue);  // Will fail validation

try
{
    await batch.DisposeAsync();  // Throws, database unchanged
}
catch (SettingsValidationException)
{
    // Both descriptor1 and descriptor2 changes discarded
}
```

### Progress Reporting

Monitor batch save progress:

```csharp
var progress = new Progress<SettingsProgress>(p =>
{
    Console.WriteLine($"Saving {p.Index}/{p.Total}: {p.SettingsModule}/{p.SettingName}");
});

var batch = manager.BeginBatch(SettingContext.Application(), progress);
// ... queue changes ...
await batch.DisposeAsync();  // Progress reports during commit
```

### Nested Batches

Nested batches are not supported and will throw `InvalidOperationException`:

```csharp
var batch1 = manager.BeginBatch();
var batch2 = manager.BeginBatch();  // ❌ Throws InvalidOperationException

// Complete batch1 before starting batch2:
await batch1.DisposeAsync();
var batch2 = manager.BeginBatch();  // ✅ OK
```

---

## Discovery and Introspection

### Descriptors by Category

Group registered descriptors by category for UI generation:

```csharp
var byCategory = manager.GetDescriptorsByCategory();
// Result: {
//   "Layout": [WindowPosition, WindowSize, ...],
//   "Appearance": [WindowOpacity, Theme, ...],
//   "": [UnategorizedSetting, ...]
// }
```

### Search Descriptors

Free-text search across module names, display names, descriptions, and categories:

```csharp
var results = manager.SearchDescriptors("opacity");
// Returns descriptors matching term (case-insensitive) in any field
```

### Enumerate All Keys

List all setting keys stored in the database:

```csharp
var keys = await manager.GetAllKeysAsync();
// Result: ["Docking/WindowPosition", "Docking/WindowSize", ...]
```

### Enumerate All Values for a Key

Retrieve all stored values for a specific key across all scopes (useful for migration, backup, or diagnostics):

```csharp
var allValues = await manager.GetAllValuesAsync("Docking/WindowPosition");
// Result: [
//   (Scope.Application, null, Point(100, 100)),
//   (Scope.Project, "C:\\project1", Point(200, 200)),
//   (Scope.Project, "C:\\project2", Point(300, 300))
// ]
```

---

## Performance and Caching

### Multi-Level Caching

The settings manager uses two layers of caching:

- **1. In-Memory Cache (`SettingsCache`)**

- Stores deserialized values keyed by `(Module, Name, Scope, ScopeId)`.
- Invalidated on save operations for the same key.
- Thread-safe via concurrent dictionary.

- **2. Descriptor Provider Cache**

- Descriptor discovery (by category, search) uses static providers to avoid reflection overhead.
- Descriptors are registered at module load time via source generators or manual registration.

### Cache Keys

Cache entries use a composite key:

```csharp
var cacheKey = $"{module}:{name}:{scope}:{scopeId}";
// Example: "Docking:WindowPosition:1:C:\projects\MyGame"
```

### Invalidation

When a setting is saved, the cache for that specific key is invalidated:

```csharp
await manager.SaveSettingAsync(key, value);  // Cache entry removed
var reloaded = await manager.LoadSettingAsync(key);  // Database query
```

Batch saves invalidate all affected keys upon successful commit.

### Performance Characteristics

- **First Load**: Database query + JSON deserialization (~5-20ms depending on serialization complexity).
- **Cached Load**: Dictionary lookup (~<1ms).
- **Save**: Database write + cache invalidation (~10-50ms).
- **Batch Save**: Single transaction + multiple writes (~50-200ms).

Database indexes on `(SettingsModule, Name, Scope, ScopeId)` ensure efficient lookups.

---

## Integration Guide

### Step 1: Define Your Settings Descriptors

Create a descriptor set class for your module:

```csharp
namespace MyModule.Settings;

public sealed class MyModuleSettingsDescriptors : SettingsDescriptorSet
{
    [Display(Name = "Feature Enabled", Category = "Features")]
    public static readonly SettingDescriptor<bool> FeatureEnabled =
        CreateDescriptor<bool>("MyModule", "FeatureEnabled");

    [Display(Name = "Timeout (ms)", Category = "Performance")]
    [Range(100, 10000)]
    public static readonly SettingDescriptor<int> TimeoutMs =
        CreateDescriptor<int>("MyModule", "TimeoutMs");
}
```

### Step 2: Register Descriptors

Register your descriptors with the static provider (typically in a module initializer):

```csharp
[System.Runtime.CompilerServices.ModuleInitializer]
public static void Initialize()
{
    EditorSettingsManager.StaticProvider.Register(
        MyModuleSettingsDescriptors.FeatureEnabled,
        MyModuleSettingsDescriptors.TimeoutMs
    );
}
```

Or let the source generator handle registration automatically via `[Persisted]` attributes (see `source-generator.md`).

### Step 3: Inject and Use

#### Option A: Using Settings Objects (Recommended for UI/ViewModels)

Define a `ModuleSettings` subclass with persisted properties:

```csharp
public sealed partial class DockingSettings : ModuleSettings
{
    private Point windowPosition = new(100, 100);
    private double windowOpacity = 1.0;

    public Point WindowPosition
    {
        get => this.windowPosition;
        set => this.SetProperty(ref this.windowPosition, value);
    }

    [Range(0.0, 1.0)]
    public double WindowOpacity
    {
        get => this.windowOpacity;
        set => this.SetProperty(ref this.windowOpacity, value);
    }

    public DockingSettings() : base("Docking") { }
}

// In your ViewModel or service:
public sealed class DockingService
{
    private readonly IEditorSettingsManager settingsManager;
    private DockingSettings settings = new();

    public async Task InitializeAsync()
    {
        await this.settings.LoadAsync(this.settingsManager);
        this.ApplyLayout(this.settings.WindowPosition, this.settings.WindowOpacity);
    }

    public async Task UpdateLayoutAsync(Point position)
    {
        this.settings.WindowPosition = position;  // Auto-validated, marks dirty
        await this.settings.SaveAsync(this.settingsManager);  // Batch-saves all modified properties
    }
}
```

#### Option B: Using Manager API Directly (For Cross-Module or Scope-Specific Operations)

```csharp
public sealed class SettingsService
{
    private readonly IEditorSettingsManager settingsManager;

    public async Task SaveCrossModuleLayoutAsync(Point position, string theme)
    {
        // Atomic save across multiple modules with explicit scope
        using var batch = this.settingsManager.BeginBatch(SettingContext.Application());
        batch
            .QueuePropertyChange(DockingSettingsDescriptors.WindowPosition, position)
            .QueuePropertyChange(UISettingsDescriptors.Theme, theme);
        await batch.DisposeAsync();
    }

    public async Task ApplyProjectOverridesAsync(string projectPath, Point position)
    {
        // Save project-specific override
        await this.settingsManager.SaveSettingAsync(
            DockingSettingsDescriptors.WindowPosition,
            position,
            SettingContext.Project(projectPath)
        );
    }
}
```

### Step 4: Persist Changes

#### Using Settings Objects

```csharp
public async Task SaveAsync()
{
    settings.WindowPosition = new Point(100, 100);
    settings.WindowOpacity = 0.95;
    await settings.SaveAsync(manager);  // Implicitly batches all modified properties
}
```

#### Using Manager API (When Atomicity Across Modules Is Needed)

```csharp
using var batch = manager.BeginBatch(SettingContext.Application());

batch
    .QueuePropertyChange(DockingSettingsDescriptors.WindowPosition, new Point(100, 200))
    .QueuePropertyChange(DockingSettingsDescriptors.WindowSize, new Size(1920, 1080));

await batch.DisposeAsync();
```

### Step 5: Subscribe to Changes

React to setting changes in your UI or services:

```csharp
private void SetupNotifications()
{
    this.settingsManager.WhenSettingChanged(DockingSettingsDescriptors.WindowPosition)
        .Subscribe(evt =>
        {
            this.logger.LogInformation("Position changed to {Position}", evt.NewValue);
            this.UpdateLayout(evt.NewValue);
        });
}
```

---

## Related Documentation

- [Data Model](./data-model.md) — Database schema, entities, and storage design.
- [Database Maintenance](./db-maintenance.md) — EF Core tooling, migrations, and operational procedures.
- [Source Generator](./source-generator.md) — Automatic descriptor and initializer generation from `[Persisted]` attributes.
