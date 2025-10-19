# Settings Service Type Registry Architecture

## Problem Solved

The `SettingsManager` cannot instantiate the abstract `SettingsService<TSettings>` class directly. We need a mechanism to map `TSettings` interface types to their concrete service implementation classes.

## Solution: Service Factory Registry

### Architecture Components

1. **Abstract Base Class**: `SettingsService<TSettings>`
2. **Concrete Implementations**: Application-specific service classes (e.g., `EditorSettingsService`)
3. **Factory Registry**: `SettingsManager` maintains a registry of factories
4. **Registration API**: `RegisterServiceFactory<TSettings, TService>()`

### Implementation

#### SettingsManager Service Registry

```csharp
public sealed partial class SettingsManager : ISettingsManager
{
    // Factory registry: TSettings type -> factory function
    private readonly ConcurrentDictionary<Type, Func<SettingsManager, ILoggerFactory?, object>> serviceFactories = new();

    // Service instance cache
    private readonly ConcurrentDictionary<Type, object> serviceInstances = new();

    /// <summary>
    /// Registers a factory for creating a concrete settings service for the specified settings type.
    /// </summary>
    public void RegisterServiceFactory<TSettings, TService>()
        where TSettings : class
        where TService : SettingsService<TSettings>
    {
        var settingsType = typeof(TSettings);
        var serviceType = typeof(TService);

        if (!this.serviceFactories.TryAdd(
            settingsType,
            (manager, loggerFactory) =>
            {
                // Use Activator to create instance with constructor parameters
                var instance = Activator.CreateInstance(serviceType, manager, loggerFactory);
                return instance ?? throw new InvalidOperationException($"Failed to create instance of {serviceType.Name}");
            }))
        {
            throw new InvalidOperationException($"Service factory for {settingsType.Name} is already registered.");
        }
    }

    /// <summary>
    /// Gets or creates a settings service instance.
    /// </summary>
    public ISettingsService<TSettings> GetService<TSettings>()
        where TSettings : class
    {
        var settingsType = typeof(TSettings);

        // Return cached instance if available
        if (this.serviceInstances.TryGetValue(settingsType, out var existingService))
        {
            return (ISettingsService<TSettings>)existingService;
        }

        // Get registered factory
        if (!this.serviceFactories.TryGetValue(settingsType, out var factory))
        {
            throw new InvalidOperationException(
                $"No service factory registered for settings type {settingsType.Name}. " +
                $"Call RegisterServiceFactory<{settingsType.Name}, TServiceImpl>() during configuration.");
        }

        // Create and cache new instance
        var service = (ISettingsService<TSettings>)factory(this, loggerFactory);
        _ = this.serviceInstances.TryAdd(settingsType, service);

        return service;
    }
}
```

### Usage Pattern

#### 1. Define Settings Interface

```csharp
public interface IEditorSettings
{
    int FontSize { get; set; }
    string FontFamily { get; set; }
}
```

#### 2. Create POCO for Serialization

```csharp
public class EditorSettingsPoco
{
    public int FontSize { get; set; } = 14;
    public string FontFamily { get; set; } = "Consolas";
}
```

#### 3. Implement Concrete Settings Service

```csharp
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

#### 4. Register in DI Container

```csharp
// In bootstrapper or startup configuration
var settingsManager = container.Resolve<SettingsManager>();

// Register the concrete service type for the settings interface
settingsManager.RegisterServiceFactory<IEditorSettings, EditorSettingsService>();

// Now consumers can request the service
var editorSettings = settingsManager.GetService<IEditorSettings>();
await editorSettings.InitializeAsync();
```

#### 5. Alternative: DI Container Integration

```csharp
// Using DryIoc container registration
container.Register<SettingsManager>(Reuse.Singleton);

// Register service factory during container setup
container.RegisterDelegate<SettingsManager>(manager =>
{
    manager.RegisterServiceFactory<IEditorSettings, EditorSettingsService>();
    return manager;
});

// Register the service accessor
container.Register<ISettingsService<IEditorSettings>>(
    made: Made.Of(
        r => ServiceInfo.Of<SettingsManager>(),
        manager => manager.GetService<IEditorSettings>()));
```

### Benefits

1. **Type Safety**: Compiler ensures concrete service implements the correct interface
2. **Flexibility**: Different concrete implementations for the same interface in different contexts
3. **Lazy Initialization**: Services are only created when first requested
4. **Singleton Pattern**: Each settings type gets a single service instance (cached)
5. **Error Handling**: Clear error messages when factory is not registered
6. **Testability**: Easy to mock or swap implementations for testing

### Error Messages

If a service is requested without registration:

```
InvalidOperationException: No service factory registered for settings type IEditorSettings.
Call RegisterServiceFactory<IEditorSettings, EditorSettingsService>() during configuration.
```

### Activator.CreateInstance

The factory uses `Activator.CreateInstance` to instantiate the concrete service class with parameters:
- `manager`: The `SettingsManager` instance
- `loggerFactory`: Optional `ILoggerFactory` for logging

The concrete service constructor must match this signature:
```csharp
public ConcreteService(SettingsManager manager, ILoggerFactory? loggerFactory = null)
    : base(manager, loggerFactory)
{
}
```

### Thread Safety

- `ConcurrentDictionary` ensures thread-safe factory registration and service instance caching
- First access to a service type creates the instance (lazy initialization)
- Subsequent accesses return the cached instance
- Factory registration is typically done during startup (single-threaded)
