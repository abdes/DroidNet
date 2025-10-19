# SettingsService Refactoring Summary

## Problem

The original `SettingsService<TSettings>` implementation had a fundamental design flaw:

- It was **sealed** and tried to create instances of `TSettings` using `new TSettings()`
- This required `TSettings` to have a parameterless constructor constraint: `where TSettings : class, new()`
- **However**, `TSettings` is designed to be an **interface**, not a concrete class

## Solution

The refactoring changes `SettingsService<TSettings>` from a sealed concrete class to an **abstract base class** following the template method pattern.

### Key Changes

#### 1. Class Declaration
**Before:**
```csharp
public sealed class SettingsService<TSettings> : ISettingsService<TSettings>
    where TSettings : class
```

**After:**
```csharp
public abstract class SettingsService<TSettings> : ISettingsService<TSettings>
    where TSettings : class
```

#### 2. Settings Property
**Before:**
```csharp
private TSettings settings;

public TSettings Settings
{
    get => this.settings;
}
```

**After:**
```csharp
public TSettings Settings
{
    get
    {
        this.ThrowIfDisposed();
        return (TSettings)(object)this; // The derived class IS the TSettings implementation
    }
}
```

#### 3. Abstract Methods for Derived Classes

Derived classes must implement these three abstract methods:

```csharp
/// <summary>
/// Creates a POCO snapshot of the current settings state for serialization.
/// </summary>
protected abstract object GetSettingsSnapshot();

/// <summary>
/// Updates the properties of this service from a loaded settings POCO.
/// </summary>
protected abstract void UpdateProperties(TSettings settings);

/// <summary>
/// Creates a new instance with default settings values.
/// </summary>
protected abstract TSettings CreateDefaultSettings();
```

#### 4. Constructor Visibility
**Before:**
```csharp
internal SettingsService(...) { }
```

**After:**
```csharp
protected SettingsService(...) { }
```

#### 5. Dispose Pattern
Properly implements the disposable pattern for abstract classes:

```csharp
public void Dispose()
{
    this.Dispose(disposing: true);
    GC.SuppressFinalize(this);
}

protected virtual void Dispose(bool disposing)
{
    if (this.isDisposed)
    {
        return;
    }

    if (disposing)
    {
        this.operationLock.Dispose();
    }

    this.isDisposed = true;
}
```

### Usage Pattern

Concrete settings service classes now follow this pattern:

```csharp
public interface IEditorSettings
{
    int FontSize { get; set; }
    string FontFamily { get; set; }
}

public class EditorSettingsPoco
{
    public int FontSize { get; set; } = 14;
    public string FontFamily { get; set; } = "Consolas";
}

public class EditorSettingsService : SettingsService<IEditorSettings>, IEditorSettings
{
    private int fontSize = 14;
    private string fontFamily = "Consolas";

    public EditorSettingsService(SettingsManager manager, ILoggerFactory? loggerFactory = null)
        : base(manager, loggerFactory)
    {
    }

    // Implement IEditorSettings
    public int FontSize
    {
        get => this.fontSize;
        set
        {
            if (this.fontSize != value)
            {
                this.fontSize = value;
                this.OnPropertyChanged();
                this.IsDirty = true;
            }
        }
    }

    public string FontFamily
    {
        get => this.fontFamily;
        set
        {
            if (this.fontFamily != value)
            {
                this.fontFamily = value;
                this.OnPropertyChanged();
                this.IsDirty = true;
            }
        }
    }

    // Implement abstract methods
    protected override object GetSettingsSnapshot()
    {
        return new EditorSettingsPoco
        {
            FontSize = this.fontSize,
            FontFamily = this.fontFamily
        };
    }

    protected override void UpdateProperties(IEditorSettings settings)
    {
        this.FontSize = settings.FontSize;
        this.FontFamily = settings.FontFamily;
    }

    protected override IEditorSettings CreateDefaultSettings()
    {
        return new EditorSettingsPoco();
    }
}
```

### SettingsManager Changes

Removed the `new()` constraint from `LoadSettingsAsync`:

**Before:**
```csharp
internal async Task<TSettings> LoadSettingsAsync<TSettings>(CancellationToken cancellationToken = default)
    where TSettings : class, new()
{
    var mergedSettings = new TSettings(); // Required new() constraint
    // ...
}
```

**After:**
```csharp
internal async Task<TSettings> LoadSettingsAsync<TSettings>(CancellationToken cancellationToken = default)
    where TSettings : class
{
    TSettings? mergedSettings = null;
    // ...
    return mergedSettings ?? throw new InvalidOperationException($"No settings data found for type {settingsTypeName}");
}
```

## Benefits

1. **Proper Abstraction**: `TSettings` can now be an interface as intended
2. **Type Safety**: The service itself implements the settings interface, providing compile-time type safety
3. **Flexibility**: Derived classes have full control over property implementation (validation, notifications, etc.)
4. **Separation of Concerns**:
   - Base class handles persistence, validation, and lifecycle
   - Derived class handles property storage and change tracking
5. **Testability**: Mock implementations can be created for testing
