# Editor.Data Module - API Modernization Proposal

## Executive Summary

This document proposes comprehensive API improvements for the Editor.Data module's settings management system. The design addresses key limitations: lack of type safety, missing validation, no scope hierarchy, and poor discoverability. All proposals leverage existing .NET patterns and avoid reinventing the wheel.

## 1. Type-Safe Setting Keys

**Problem:** String-based keys are error-prone and refactoring-hostile.

**Solution:** Strongly-typed keys with structured components optimized for EF Core.

```csharp
public readonly record struct SettingKey<T>(string SettingsModule, string Name)
{
    // For display/logging
    public string ToPath() => $"{SettingsModule}/{Name}";

    // For serialization scenarios
    public static implicit operator string(SettingKey<T> key) => key.ToPath();

    // Centralized parsing (no ad-hoc string manipulation)
    public static SettingKey<T> Parse(string path)
    {
        var parts = path.Split('/', 2);
        if (parts.Length != 2)
                throw new FormatException($"Invalid setting key: '{path}'. Expected format: Module/Name");
            return new SettingKey<T>(parts[0], parts[1]);
    }
}
```

**Benefits:**

- Compile-time type safety prevents mismatched value types
- Module and Name accessible as properties (no parsing in EF queries)
- Type parameter enables generic constraint validation
- Explicit parsing/formatting via ToPath() and Parse() methods

## 2. Reactive Change Notifications

**Problem:** Current callback-based change notifications are primitive and non-composable.

**Solution:** `IObservable<T>` integration for Rx.NET composition.

```csharp
public readonly record struct SettingChangedEvent<T>(
    SettingKey<T> Key,
    T? OldValue,
    T NewValue,
    SettingScope Scope,
    string? ScopeId);

public interface IEditorSettingsManager
{
    IObservable<SettingChangedEvent<T>> WhenSettingChanged<T>(SettingKey<T> key);
}

// Usage: Debounce, throttle, combine with other observables
manager.WhenSettingChanged(DockingSettingsDescriptors.WindowPosition.Key)
    .Throttle(TimeSpan.FromMilliseconds(500))
    .DistinctUntilChanged()
    .Subscribe(evt => UpdateLayout(evt.NewValue));
```

**Benefits:**

- Composable with Rx operators (throttle, debounce, buffer, combine)
- Strongly-typed event payload
- Scope information included for debugging
- Supports filtering by scope or module

## 3. Atomic Bulk Operations

**Problem:** No transactional support for saving multiple related settings.

**Solution:** Fluent batch API with atomic commit semantics.

```csharp
public interface ISettingsBatch
{
    ISettingsBatch Set<T>(SettingKey<T> key, T value);
    ISettingsBatch Remove<T>(SettingKey<T> key);
}

public interface IEditorSettingsManager
{
    // Atomic batch operation
    Task SaveSettingsAsync(
        Action<ISettingsBatch> configure,
        CancellationToken ct = default);
}

// Usage: All-or-nothing transactional save
await manager.SaveSettingsAsync(batch => batch
    .Set(DockingSettingsDescriptors.WindowPosition.Key, new Point(100, 200))
    .Set(DockingSettingsDescriptors.WindowWidth.Key, 1024)
    .Set(DockingSettingsDescriptors.Theme.Key, "Dark"));
```

**Benefits:**

- Single database transaction for consistency
- Fluent API for readability
- Single change notification for batch (performance)
- Rollback on validation failure

## 4. Hierarchical Setting Scopes

**Problem:** All settings are global; no per-project customization.

**Solution:** Two-tier scope hierarchy with automatic fallback resolution.

### Design

```csharp
public enum SettingScope
{
    Application = 0,  // User's AppData (global defaults)
    Project = 1       // Per-project overrides
}

public sealed record SettingContext(SettingScope Scope, string? ScopeId = null)
{
    public static SettingContext Application() => new(SettingScope.Application);
    public static SettingContext Project(string projectPath) => new(SettingScope.Project, projectPath);
}
```

### API

```csharp
public interface IEditorSettingsManager
{
    // Explicit scope operations (no fallback)
    Task SaveSettingAsync<T>(SettingKey<T> key, T value, SettingContext context, CancellationToken ct = default);
    Task<T?> LoadSettingAsync<T>(SettingKey<T> key, SettingContext context, CancellationToken ct = default);
    Task DeleteSettingAsync<T>(SettingKey<T> key, SettingContext context, CancellationToken ct = default);

    // Hierarchical resolution with automatic fallback: Project → Application → Model default
    Task<T> ResolveSettingAsync<T>(SettingKey<T> key, string? projectId = null, CancellationToken ct = default);

    // Diagnostic: which scopes have values for this key?
    Task<IReadOnlyList<SettingScope>> GetDefinedScopesAsync<T>(SettingKey<T> key, string? projectId = null, CancellationToken ct = default);
}
```

### Usage Example

```csharp
// Set application-wide default
await manager.SaveSettingAsync(
    DockingSettingsDescriptors.Theme.Key,
    "Dark",
    SettingContext.Application());

// Override for specific project
await manager.SaveSettingAsync(
    DockingSettingsDescriptors.Theme.Key,
    "HighContrast",
    SettingContext.Project(@"C:\Projects\MyGame"));

// Resolve: automatically walks hierarchy
var theme = await manager.ResolveSettingAsync(
    DockingSettingsDescriptors.Theme.Key,
    projectId: @"C:\Projects\MyGame");
// Result: "HighContrast" (project overrides application)

// Check where overrides exist
var scopes = await manager.GetDefinedScopesAsync(
    DockingSettingsDescriptors.Theme.Key,
    projectId: @"C:\Projects\MyGame");
// Result: [Application, Project]
```

**Benefits:**

- Clear separation: explicit ops vs. automatic resolution
- No User scope (each Windows user has own AppData)
- ScopeId for project is project path (natural key)
- Resolution order is deterministic and documented

### Database Schema

```sql
CREATE TABLE ModuleSetting (
    SettingsModule TEXT NOT NULL,
    Name TEXT NOT NULL,
    Scope INTEGER NOT NULL,
    ScopeId TEXT,                   -- NULL for Application scope
    Value TEXT NOT NULL,            -- JSON-serialized (see Value Serialization below)
    CreatedAt DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    ModifiedAt DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (SettingsModule, Name, Scope, COALESCE(ScopeId, ''))
);

CREATE INDEX IX_ModuleSetting_Lookup ON ModuleSetting(SettingsModule, Name, Scope);
CREATE INDEX IX_ModuleSetting_Scope ON ModuleSetting(Scope, ScopeId);
```

### Value Serialization

**Problem:** Settings have heterogeneous types (int, double, string, Point, custom records). EF Core cannot natively persist arbitrary .NET types in a single column.

**Solution:** JSON serialization for the `Value` column. Type safety is enforced at the **API layer**, not the database schema.

```csharp
// Serialization examples
int → "42"
double → "3.14"
string → "\"Dark\""
Point → "{\"x\":100,\"y\":200}"
Size → "{\"width\":1024,\"height\":768}"
CustomRecord → "{\"prop1\":\"value1\",\"prop2\":123}"
```

**Type Safety Guarantee:**

The generic type parameter `T` in `SettingKey<T>` ensures compile-time type safety:

```csharp
SettingKey<Point> windowPos = new("Docking", "WindowPosition");

// Compiler enforces type correctness
Point pos = await manager.LoadSettingAsync(windowPos);  // ✅ Returns Point
await manager.SaveSettingAsync(windowPos, new Point(100, 200));  // ✅ Accepts Point
await manager.SaveSettingAsync(windowPos, 42);  // ❌ Compile error
```

**Performance Considerations:**

- Primitive types (int, double, string, bool): JSON overhead is trivial (`42` vs `"42"`)
- Structured types (Point, Size): Minimal overhead, stored as compact JSON objects
- Complex types: Standard System.Text.Json serialization (high performance)
- Caching layer: Deserialized values cached in memory after first load

**Why Not Separate Columns?**

Alternatives like separate `IntValue`, `DoubleValue`, `StringValue`, `PointX`, `PointY` columns were rejected because:

- Schema fragility: Adding new setting types requires database migration
- NULL proliferation: Most columns NULL for any given row
- Type discrimination complexity: Requires additional metadata to determine which column to read
- No benefit: JSON serialization is performant and flexible

### EF Core Configuration

```csharp
public class ModuleSetting
{
    public string SettingsModule { get; set; } = null!;
    public string Name { get; set; } = null!;
    public SettingScope Scope { get; set; }
    public string? ScopeId { get; set; }
    public string Value { get; set; } = null!;  // JSON
    public DateTime CreatedAt { get; set; }
    public DateTime ModifiedAt { get; set; }
}

public void Configure(EntityTypeBuilder<ModuleSetting> builder)
{
    builder.HasKey(s => new { s.SettingsModule, s.Name, s.Scope, s.ScopeId });
    builder.Property(s => s.Value).IsRequired();
    builder.Property(s => s.CreatedAt).HasDefaultValueSql("CURRENT_TIMESTAMP");
    builder.Property(s => s.ModifiedAt).HasDefaultValueSql("CURRENT_TIMESTAMP");

    builder.HasIndex(s => new { s.SettingsModule, s.Name, s.Scope });
    builder.HasIndex(s => new { s.Scope, s.ScopeId });
}

// Query pattern (no string parsing)
var setting = await context.ModuleSettings
    .FirstOrDefaultAsync(s =>
        s.SettingsModule == key.SettingsModule &&
        s.Name == key.Name &&
        s.Scope == scope &&
        s.ScopeId == scopeId,
        ct);
```

**Benefits:**

- Composite PK prevents duplicate entries
- Indexes optimized for resolution queries
- Audit timestamps for diagnostics
- Type-safe queries (no string manipulation)

## 5. Setting Descriptors with Metadata

**Problem:** Settings lack discoverability, validation constraints, and metadata for auto-generated UIs.

**Solution:** Descriptors as strongly-typed properties with Data Annotations.

### Design

```csharp
public abstract class SettingsDescriptorSet
{
    // Helper extracts metadata from the calling property by name
    protected static SettingDescriptor<T> CreateDescriptor<T>(string module, string name, [CallerMemberName] string? propertyName = null)
    {
        var property = typeof(SettingsDescriptorSet).GetProperty(propertyName!, BindingFlags.Public | BindingFlags.Static);
        if (property == null)
            throw new InvalidOperationException($"Property {propertyName} not found");

        return new SettingDescriptor<T>
        {
            Key = new SettingKey<T>(module, name),
            DisplayName = property.GetCustomAttribute<DisplayAttribute>()?.Name,
            Description = property.GetCustomAttribute<DisplayAttribute>()?.Description,
            Category = property.GetCustomAttribute<CategoryAttribute>()?.Category,
            Validators = property.GetCustomAttributes<ValidationAttribute>().ToList()
        };
    }
}

public sealed class SettingDescriptor<T>
{
    public required SettingKey<T> Key { get; init; }
    public string? DisplayName { get; init; }
    public string? Description { get; init; }
    public string? Category { get; init; }
    public IReadOnlyList<ValidationAttribute> Validators { get; init; } = [];
}
```

### Defining Descriptors

```csharp
public class DockingSettingsDescriptors : SettingsDescriptorSet
{
    [Display(Name = "Window Opacity", Description = "Transparency level (0.0-1.0)")]
    [Category("Appearance")]
    [Range(0.0, 1.0, ErrorMessage = "Opacity must be between 0 and 1")]
    public static SettingDescriptor<double> Opacity { get; } =
        CreateDescriptor<double>("Docking", "Opacity");

    [Display(Name = "Color Theme")]
    [Category("Appearance")]
    [Required]
    [RegularExpression("Light|Dark|HighContrast", ErrorMessage = "Invalid theme")]
    public static SettingDescriptor<string> Theme { get; } =
        CreateDescriptor<string>("Docking", "Theme");

    [Display(Name = "Window Width", Description = "Main window width in pixels")]
    [Category("Layout")]
    [Range(800, 7680)]
    public static SettingDescriptor<int> WindowWidth { get; } =
        CreateDescriptor<int>("Docking", "WindowWidth");

    [Display(Name = "Window Position")]
    [Category("Layout")]
    public static SettingDescriptor<Point> WindowPosition { get; } =
        CreateDescriptor<Point>("Docking", "WindowPosition");
}

// Model object defines default values
public class DockingSettings
{
    public double Opacity { get; set; } = 1.0;
    public string Theme { get; set; } = "Light";
    public int WindowWidth { get; set; } = 1280;
    public Point WindowPosition { get; set; } = new(100, 100);
}

// Usage - descriptors contain both keys and metadata
await manager.SaveSettingAsync(DockingSettingsDescriptors.WindowPosition.Key, new Point(200, 200));
var position = await manager.LoadSettingAsync(DockingSettingsDescriptors.WindowPosition.Key);  // Type inferred as Point
```

### Validation Integration

The settings manager validates values automatically before saving when a descriptor is provided:

```csharp
public interface IEditorSettingsManager
{
    Task SaveSettingAsync<T>(SettingDescriptor<T> descriptor, T value, CancellationToken ct = default);
    Task SaveSettingAsync<T>(SettingDescriptor<T> descriptor, T value, SettingContext context, CancellationToken ct = default);
}

// Implementation validates using descriptor's Validators
public async Task SaveSettingAsync<T>(SettingDescriptor<T> descriptor, T value, CancellationToken ct = default)
{
    foreach (var validator in descriptor.Validators)
    {
        var validationContext = new ValidationContext(value) { DisplayName = descriptor.DisplayName ?? descriptor.Key.ToPath() };
        var result = validator.GetValidationResult(value, validationContext);
        if (result != ValidationResult.Success)
        {
            throw new SettingsValidationException(descriptor.Key, result.ErrorMessage ?? "Validation failed");
        }
    }

    await SaveSettingAsync(descriptor.Key, value);
}
```

### Usage

```csharp
// Option 1: Use descriptor directly (includes validation)
await manager.SaveSettingAsync(DockingSettingsDescriptors.Opacity, 0.8);  // ✅ Valid
await manager.SaveSettingAsync(DockingSettingsDescriptors.Opacity, 1.5);  // ❌ Throws SettingsValidationException

// Option 2: Use key directly (no validation)
await manager.SaveSettingAsync(DockingSettingsDescriptors.Opacity.Key, 0.8);

// Descriptors provide metadata for UI generation
var descriptor = DockingSettingsDescriptors.Opacity;
// descriptor.Key.SettingsModule = "Docking"
// descriptor.Key.Name = "Opacity"
// descriptor.Key.ToPath() = "Docking/Opacity"
// descriptor.DisplayName = "Window Opacity"
// descriptor.Description = "Transparency level (0.0-1.0)"
// descriptor.Category = "Appearance"
// descriptor.Validators = [RangeAttribute(0.0, 1.0)]
```

**Benefits:**

- Validation rules colocated with metadata (single source of truth)
- Standard .NET Data Annotations (no custom validation framework)
- UI can introspect validators to generate appropriate controls
- Automatic server-side validation before persistence
- Same attributes work for client-side validation in WPF/WinUI

## 6. Discovery and Enumeration

**Problem:** No way to programmatically discover settings, list modules, or inspect configuration.

**Solution:** Use-case driven query APIs.

```csharp
// Use Case 1: Building a Settings UI
Task<IReadOnlyDictionary<string, IReadOnlyList<ISettingDescriptor>>> GetDescriptorsByCategoryAsync(CancellationToken ct = default);

// Use Case 2: Settings Search
Task<IReadOnlyList<ISettingDescriptor>> SearchDescriptorsAsync(string searchTerm, CancellationToken ct = default);

// Use Case 3: Diagnostics and Debugging
Task<IReadOnlyList<string>> GetAllKeysAsync(CancellationToken ct = default);
Task<IReadOnlyList<(SettingScope Scope, string? ScopeId, object? Value)>> GetAllValuesAsync(string key, CancellationToken ct = default);
```

### Use Case 1: Building a Settings UI

Scenario: Generate a settings editor that organizes settings by category with proper controls

```csharp
// Get all registered descriptors grouped by category
Task<IReadOnlyDictionary<string, IReadOnlyList<ISettingDescriptor>>> GetDescriptorsByCategoryAsync(CancellationToken ct = default);

// Usage
var descriptorsByCategory = await manager.GetDescriptorsByCategoryAsync();
foreach (var (category, descriptors) in descriptorsByCategory)
{
    // Create UI section for category
    foreach (var descriptor in descriptors)
    {
        // Generate appropriate control based on descriptor.Validators
        // e.g., Slider for [Range], TextBox for string, ComboBox for [RegularExpression]
    }
}
```

### Use Case 2: Settings Search

Scenario: User searches for "opacity" in settings UI

```csharp
// Search across keys, display names, and descriptions
Task<IReadOnlyList<ISettingDescriptor>> SearchDescriptorsAsync(string searchTerm, CancellationToken ct = default);

// Usage
var results = await manager.SearchDescriptorsAsync("opacity");
// Returns: [DockingSettingsDescriptors.Opacity, ...]
```

### Use Case 3: Diagnostics and Debugging

Scenario: Show which settings are overridden at project level vs application level

```csharp
// Get all keys that have values in database (any scope)
Task<IReadOnlyList<string>> GetAllKeysAsync(CancellationToken ct = default);

// Get value and source for a specific key across all scopes
Task<IReadOnlyList<(SettingScope Scope, string? ScopeId, object? Value)>> GetAllValuesAsync(string key, CancellationToken ct = default);

// Usage: Diagnostic view
var allKeys = await manager.GetAllKeysAsync();
foreach (var key in allKeys)
{
    var values = await manager.GetAllValuesAsync(key);
    Console.WriteLine($"{key}:");
    foreach (var (scope, scopeId, value) in values)
    {
        Console.WriteLine($"  {scope}{(scopeId != null ? $" ({scopeId})" : "")}: {value}");
    }
}
// Output:
// Editor/Theme:
//   Application: Dark
//   Project (C:\MyGame): HighContrast
```

## 7. Modern Async Patterns

**Problem:** Missing cancellation tokens and progress reporting for long operations.

**Solution:** `CancellationToken` and `IProgress<T>` support throughout.

```csharp
public readonly record struct SettingsSaveProgress(
    int TotalSettings,
    int CompletedSettings,
    string CurrentModule);

public interface IEditorSettingsManager
{
    // All async methods accept cancellation tokens
    Task SaveSettingAsync<T>(
        SettingKey<T> key,
        T value,
        SettingContext? context = null,
        CancellationToken cancellationToken = default);

    // Bulk operations with progress reporting
    Task SaveSettingsAsync(
        Action<ISettingsBatch> configure,
        IProgress<SettingsSaveProgress>? progress = null,
        CancellationToken cancellationToken = default);

    // Export with progress for large data sets
    Task<IDictionary<string, object?>> ExportScopeAsync(
        SettingContext context,
        IProgress<int>? progress = null,
        CancellationToken cancellationToken = default);
}

// Usage: Bulk save with timeout and progress
var cts = new CancellationTokenSource(TimeSpan.FromSeconds(30));
var progress = new Progress<SettingsSaveProgress>(p =>
    Console.WriteLine($"[{p.CurrentModule}] {p.CompletedSettings}/{p.TotalSettings}"));

await manager.SaveSettingsAsync(
    batch => batch
        .Set(DockingSettingsDescriptors.WindowPosition.Key, new Point(100, 200))
        .Set(DockingSettingsDescriptors.Theme.Key, "Dark"),
    progress,
    cts.Token);
```

**Benefits:**

- Cancellation support prevents hung operations
- Progress reporting for responsive UIs
- Consistent with modern async .NET patterns
- Timeout support via CancellationTokenSource

---

## Implementation Priorities

### Phase 1: Foundation (Breaking Changes)

1. Introduce `SettingKey<T>` with Module/Name structure
2. Update database schema (migration required)
3. Add scope support (Application/Project)
4. Update EF Core entities and queries

### Phase 2: Enhanced APIs (Additive)

1. Implement descriptor system with Data Annotations
2. Add batch operations with transactions
3. Implement hierarchical resolution
4. Add discovery/enumeration APIs

### Phase 3: Advanced Features (Optional)

1. Reactive change notifications (IObservable)
2. Progress reporting for bulk operations
3. Setting descriptors registry for UI generation
4. Export/import with schema versioning

## Migration Strategy

1. **Database migration:** EF Core migration to restructure schema with composite key (Module, Name, Scope, ScopeId)
2. **Data migration:** One-time script to parse existing `Key` strings into Module/Name components
3. **Clean break:** Remove all old APIs - no deprecation period
4. **Testing:** Comprehensive unit tests for resolution logic, scope fallback, validation, and migration correctness

## Risk Assessment

| Risk | Mitigation |
|------|------------|
| Data loss during migration | Comprehensive migration tests; backup validation before deployment |
| Performance regression from validation | Opt-in validation via descriptors; direct key access skips validation |
| Complex resolution logic introduces bugs | Extensive unit tests, property-based tests for resolution invariants |
| Reflection overhead in descriptors | One-time initialization at startup; cache results |

## Conclusion

This proposal delivers a modern, type-safe settings API built on .NET standards (Data Annotations, Rx.NET, EF Core). The clean break eliminates technical debt and establishes a solid foundation. Migration is straightforward: update database schema, run data migration script, update consuming code to new APIs.
