# Settings Batch Operations

The settings system provides batch operations to efficiently save multiple settings atomically. Changes within a batch are validated and committed as a single database transaction, ensuring consistency and reducing I/O overhead.

## Overview

Batch operations provide several key benefits:

- **Atomicity**: All changes commit or roll back together
- **Performance**: Single database transaction reduces overhead
- **Validation**: All settings validated before commit
- **Progress Reporting**: Track batch progress for UI feedback
- **Thread Safety**: AsyncLocal-based isolation for concurrent operations

## Basic Usage

### Manual Batch with Descriptors

Use `BeginBatch()` to create a batch and `QueuePropertyChange<T>()` to add settings with their descriptors:

```csharp
var manager = serviceProvider.GetRequiredService<EditorSettingsManager>();

var batch = manager.BeginBatch();
await using (batch)
{
    // Queue changes with descriptors
    batch.QueuePropertyChange(WindowSettings.Descriptors.Position, new Point(100, 100));
    batch.QueuePropertyChange(WindowSettings.Descriptors.Size, new Size(1920, 1080));
    batch.QueuePropertyChange(EditorSettings.Descriptors.Theme, "Dark");

    // Batch commits on disposal
}
```

**Key Points:**

- Descriptors are **required** - they provide validation rules and metadata
- Each `QueuePropertyChange()` call requires a `SettingDescriptor<T>` and typed value
- Batch commits automatically on disposal via `DisposeAsync()`
- On error, the entire batch rolls back - no partial commits

### Automatic Batch Detection with ModuleSettings

`ModuleSettings` subclasses automatically detect active batches through AsyncLocal context:

```csharp
var manager = serviceProvider.GetRequiredService<EditorSettingsManager>();
var settings = new WindowSettings();

var batch = manager.BeginBatch();
await using (batch)
{
    // Property setters automatically queue to the batch
    settings.Position = new Point(100, 100);
    settings.Size = new Size(1920, 1080);

    // Changes commit when batch disposes
}
```

**How It Works:**

1. `BeginBatch()` stores the batch in `AsyncLocal<SettingsBatch>`
2. `ModuleSettings.SetProperty()` checks `SettingsBatch.Current`
3. If a batch exists, changes are queued automatically
4. Descriptors are resolved via reflection from the nested `Descriptors` class
5. On disposal, all queued changes commit in a single transaction

### Scoped Batches

Specify a `SettingContext` to scope all batch operations:

```csharp
// Application-level settings
var batch1 = manager.BeginBatch(SettingContext.Application());
await using (batch1)
{
    settings.Theme = "Dark";
}

// Project-specific settings
var batch2 = manager.BeginBatch(SettingContext.Project("MyProject"));
await using (batch2)
{
    settings.WindowPosition = new Point(0, 0);
}

// User-specific settings
var batch3 = manager.BeginBatch(SettingContext.User("john.doe"));
await using (batch3)
{
    settings.EditorFont = "Consolas";
}
```

All settings queued in a scoped batch inherit the specified context.

## Concurrent Batches

### Thread Safety with AsyncLocal

The batch system uses `AsyncLocal<SettingsBatch>` to provide execution context isolation. Each execution context (thread, async continuation, Task.Run) gets its own batch instance:

```csharp
var manager = serviceProvider.GetRequiredService<EditorSettingsManager>();

// Batch 1 in main thread
var batch1 = manager.BeginBatch(SettingContext.Project("Project1"));
await using (batch1)
{
    var settings1 = new WindowSettings();
    settings1.Position = new Point(10, 10);
}

// Batch 2 in Task.Run (different execution context)
await Task.Run(async () =>
{
    var batch2 = manager.BeginBatch(SettingContext.Project("Project2"));
    await using (batch2)
    {
        var settings2 = new WindowSettings();
        settings2.Position = new Point(20, 20);
    }
});
```

**AsyncLocal Behavior:**

- Each `Task.Run()` creates a new execution context with its own AsyncLocal storage
- Batches don't leak across execution contexts
- Sequential batches in the same context are isolated
- **Nested batches are forbidden** and will throw `InvalidOperationException`

### Database Concurrency

The settings system is configured with **SQLite WAL (Write-Ahead Logging)** mode for improved concurrency:

```csharp
// DatabaseTests.cs
this.dbConnection.Open();

// Enable WAL mode for better concurrency
using (var walCommand = this.dbConnection.CreateCommand())
{
    walCommand.CommandText = "PRAGMA journal_mode=WAL;";
    walCommand.ExecuteNonQuery();
}
```

**Concurrency Guidelines:**

1. **SQLite Serialization**: SQLite serializes access to a single connection
   - Use one connection per thread for best concurrency
   - Batches in different scopes will serialize at the database level

2. **Scoped Registration**: The `PersistentState` DbContext is registered with `Reuse.Scoped`:

   ```csharp
   // DI Container setup
   container.Register<PersistentState>(Reuse.Scoped);
   container.Register<EditorSettingsManager>(Reuse.Scoped);
   ```

   This ensures each DI scope gets its own DbContext instance. Since EF Core's DbContext is **not thread-safe**, this registration pattern is critical for concurrent operations.

3. **Separate Scopes for Concurrency**: Create separate DI scopes to get isolated DbContext instances

   ```csharp
   await Task.WhenAll(
       Task.Run(async () =>
       {
           await using var scope = serviceProvider.CreateScope();
           var manager = scope.ServiceProvider.GetRequiredService<EditorSettingsManager>();
           // ... batch operations
       }),
       Task.Run(async () =>
       {
           await using var scope = serviceProvider.CreateScope();
           var manager = scope.ServiceProvider.GetRequiredService<EditorSettingsManager>();
           // ... batch operations
       })
   );
   ```

   Each `CreateScope()` call creates a new scope, and due to the `Reuse.Scoped` registration, each scope gets its own `PersistentState` DbContext instance and database connection from the pool. This provides true thread-safe concurrency.

4. **Sequential Pattern**: For sequential batches, use separate `Task.Run` calls to ensure proper AsyncLocal isolation:

   ```csharp
   var scope = serviceProvider.CreateScope();
   await using (scope)
   {
       var manager = scope.ServiceProvider.GetRequiredService<EditorSettingsManager>();

       // Each Task.Run gets its own execution context
       await Task.Run(async () => {
           var batch1 = manager.BeginBatch(SettingContext.Project("A"));
           await using (batch1) { /* ... */ }
       });

       await Task.Run(async () => {
           var batch2 = manager.BeginBatch(SettingContext.Project("B"));
           await using (batch2) { /* ... */ }
       });
   }
   ```

   **Important**: Sequential batches within the same async method can have AsyncLocal context flow issues. Use `Task.Run` for proper isolation, or use separate methods.

### Nested Batches Not Supported

**Nested batches are explicitly forbidden** and will throw `InvalidOperationException`:

```csharp
var outerBatch = manager.BeginBatch();
await using (outerBatch)
{
    settings.Theme = "Dark";

    // ❌ This will throw InvalidOperationException
    var innerBatch = manager.BeginBatch();
}
```

**Reason**: AsyncLocal behavior with async/await contexts makes it impossible to reliably restore outer batch state after inner batch disposal. Attempting to support nested batches leads to subtle bugs where the wrong batch receives property changes.

**Alternative**: Complete the current batch before starting a new one:

```csharp
// ✅ Complete batches sequentially
var batch1 = manager.BeginBatch();
await using (batch1)
{
    settings.Theme = "Dark";
}

var batch2 = manager.BeginBatch(SettingContext.Project("Test"));
await using (batch2)
{
    settings.WindowSize = new Size(800, 600);
}
```

## Progress Reporting

Track batch progress with `IProgress<SettingsSaveProgress>`:

```csharp
var progress = new Progress<SettingsSaveProgress>(p =>
{
    Console.WriteLine($"Saving {p.Module}: {p.Current}/{p.Total}");
});

var batch = manager.BeginBatch(progress: progress);
await using (batch)
{
    // Queue many settings
    for (int i = 0; i < 100; i++)
    {
        batch.QueuePropertyChange(descriptor, value);
    }
    // Progress updates as each setting saves
}
```

## Validation

All settings in a batch are validated before commit:

```csharp
var batch = manager.BeginBatch();
await using (batch)
{
    // This will fail validation (negative coordinates)
    batch.QueuePropertyChange(
        WindowSettings.Descriptors.Position,
        new Point(-10, -10)
    );

    // ValidationException thrown on disposal
    // No settings are saved
}
```

**Validation Rules:**

- Descriptors define validation via `ValidationAttribute` (e.g., `[Range]`, `[PointBounds]`)
- All items validated before transaction begins
- Single validation failure rolls back entire batch
- Validation errors reported via `ValidationException` with detailed results

## Best Practices

1. **Always Use Descriptors**: Descriptors provide validation and metadata - they're required for batch operations

2. **Prefer Automatic Detection**: Let `ModuleSettings` automatically queue to batches instead of manual `QueuePropertyChange()` calls

3. **Scope Appropriately**: Use `SettingContext` to separate application, project, and user settings

4. **Handle Exceptions**: Wrap batch operations in try-catch to handle validation or persistence errors

5. **Sequential for Simplicity**: For non-concurrent scenarios, use sequential batches in a single scope - simpler and avoids SQLite concurrency limitations

6. **Separate Scopes for Concurrency**: When truly concurrent operations are needed, create separate DI scopes to get separate DbContext instances

7. **Never Nest Batches**: Complete the current batch before starting a new one. Nesting is explicitly forbidden and will throw an exception

8. **Test Isolation**: Use AsyncLocal isolation to test batch behavior without complex threading

## Example: Complex Batch Operation

```csharp
public async Task SaveProjectConfigurationAsync(
    EditorSettingsManager manager,
    ProjectConfiguration config,
    IProgress<SettingsSaveProgress>? progress = null)
{
    try
    {
        var context = SettingContext.Project(config.ProjectPath);
        var batch = manager.BeginBatch(context, progress);
        await using (batch)
        {
            var windowSettings = new WindowSettings();
            windowSettings.Position = config.WindowPosition;
            windowSettings.Size = config.WindowSize;

            var editorSettings = new EditorSettings();
            editorSettings.Theme = config.Theme;
            editorSettings.FontSize = config.FontSize;

            var buildSettings = new BuildSettings();
            buildSettings.Configuration = config.BuildConfiguration;
            buildSettings.Platform = config.Platform;

            // All settings commit atomically
        }
    }
    catch (ValidationException ex)
    {
        // Handle validation errors
        foreach (var result in ex.Results)
        {
            Console.WriteLine($"Validation error: {result.ErrorMessage}");
        }
        throw;
    }
    catch (Exception ex)
    {
        // Handle persistence errors
        Console.WriteLine($"Failed to save configuration: {ex.Message}");
        throw;
    }
}
```

## Implementation Details

### AsyncLocal Storage

```csharp
public sealed class SettingsBatch : ISettingsBatch
{
    private static readonly AsyncLocal<SettingsBatch?> CurrentBatch = new();

    public static SettingsBatch? Current => CurrentBatch.Value;

    internal SettingsBatch(...)
    {
        CurrentBatch.Value = this;
    }
}
```

### Automatic Descriptor Resolution

```csharp
protected void SetProperty<T>(ref T field, T value, [CallerMemberName] string propertyName = "")
{
    // ... validation and property change notification

    var batch = Settings.SettingsBatch.Current;
    if (batch != null)
    {
        QueuePropertyChangeInBatch(propertyName, value, batch);
    }
}

private void QueuePropertyChangeInBatch<T>(string propertyName, T? value, SettingsBatch batch)
{
    // Use reflection to find descriptor in nested Descriptors class
    var descriptorField = this.GetType()
        .GetNestedType("Descriptors", BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Static)
        ?.GetField(propertyName, BindingFlags.Public | BindingFlags.Static);

    if (descriptorField?.GetValue(null) is not SettingDescriptor<T> descriptor)
    {
        throw new InvalidOperationException($"No descriptor found for property '{propertyName}'");
    }

    batch.QueuePropertyChange(descriptor, value!);
}
```

### Transaction Commit

```csharp
internal async Task CommitBatchAsync(IReadOnlyList<BatchItem> items, ...)
{
    ValidateBatchItems(items); // Validate all items first

    var transaction = await this.context.Database.BeginTransactionAsync(ct);
    await using (transaction)
    {
        try
        {
            foreach (var item in items)
            {
                await ProcessBatchItemAsync(...);
                await this.context.SaveChangesAsync(ct);
            }

            await transaction.CommitAsync(ct);
        }
        catch
        {
            await transaction.RollbackAsync(ct);
            throw;
        }
    }
}
```

## See Also

- [Settings Overview](./settings-overview.md)
- [Validation](./validation.md)
- [Scopes and Contexts](./scopes.md)
