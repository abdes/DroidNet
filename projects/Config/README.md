# DroidNet.Config

The **DroidNet.Config** module provides two essential services for application configuration management:

1. **PathFinder**: Environment-aware path resolution for system, user, and application directories
2. **Settings Services**: Enhanced .NET Options Pattern with MVVM support, auto-save, and change tracking

Built on [DryIoc](https://github.com/dadhi/DryIoc) for dependency injection and [Microsoft.Extensions.Configuration](https://learn.microsoft.com/en-us/dotnet/api/microsoft.extensions.options?view=net-8.0-pp) for configuration loading.

## Table of Contents

- [Features](#features)
  - [PathFinder](#pathfinder)
  - [Settings Services](#settings-services)
- [Getting Started](#getting-started)
- [PathFinder](#pathfinder-1)
  - [Modes](#modes)
  - [Available Paths](#available-paths)
  - [Usage Example](#usage-example)
- [Settings Services](#settings-services-1)
  - [Architecture Overview](#architecture-overview)
  - [Usage Patterns](#usage-patterns)
  - [Registration and Consumption](#registration-and-consumption)
  - [Auto-Save Behavior](#auto-save-behavior)
  - [Why Settings Services?](#why-settings-services)
- [Complete Integration Example](#complete-integration-example)
  - [Configuration File](#configuration-file)
  - [Host Setup](#host-setup)
  - [Configuration Steps](#configuration-steps)
  - [Application Code Example](#application-code-example)
- [Testing](#testing)
  - [Running Tests](#running-tests)
  - [Running Tests with Code Coverage](#running-tests-with-code-coverage)
  - [Generating Cobertura Coverage Report](#generating-cobertura-coverage-report)
  - [Viewing Coverage in VS Code](#viewing-coverage-in-vs-code)
  - [Generating HTML Coverage Reports](#generating-html-coverage-reports)

## Features

### PathFinder

- **Environment Modes**: Switch between `development` and `real` deployment modes
- **System Paths**: Unified interface for Documents, Downloads, Desktop, OneDrive, Temp, etc.
- **Application Paths**: Automatic resolution of LocalAppData, ProgramData, configuration file locations
- **Testable**: Uses `IFileSystem` abstraction for unit testing

### Settings Services

- **MVVM Compatible**: Full `INotifyPropertyChanged` support for UI data binding
- **Auto-Save**: Configurable debounced persistence (default 5 seconds) reduces disk I/O
- **Change Tracking**: `IsDirty` property and property-specific change notifications
- **Type-Safe Access**: Clean `service.Settings.Property` API pattern
- **Framework Integration**: Extends .NET Options Pattern with UI-focused capabilities

## Getting Started

```shell
dotnet add package DroidNet.Config
```

## PathFinder

The `PathFinder` service provides environment-aware path resolution, making it easy to locate system and application directories consistently across development and production environments.

### Modes

| Mode | Purpose | LocalAppData | ProgramData |
|------|---------|--------------|-------------|
| **Development** (`dev`) | Build/debug environment | `{LocalAppData}\{Company}\{App}\Development` | `AppContext.BaseDirectory` |
| **Real** (`real`) | Production deployment | `{LocalAppData}\{Company}\{App}` | `AppContext.BaseDirectory` |

### Available Paths

The `IPathFinder` interface provides access to commonly used paths:

**User Paths**: `UserDesktop`, `UserDocuments`, `UserDownloads`, `UserHome`, `UserOneDrive`

**System Paths**: `SystemRoot`, `Temp`

**Application Paths**: `LocalAppData`, `LocalAppState`, `ProgramData`

**Methods**: `GetConfigFilePath(fileName)`, `GetProgramConfigFilePath(fileName)`

### Usage Example

```csharp
public class MyService
{
    private readonly IPathFinder pathFinder;

    public MyService(IPathFinder pathFinder)
    {
        this.pathFinder = pathFinder;

        // Get config file path
        var configPath = pathFinder.GetConfigFilePath("settings.json");

        // Access user directories
        var docsPath = pathFinder.UserDocuments;
    }
}
```

## Settings Services

Settings Services extend the .NET Options Pattern with UI-focused features like MVVM data binding, automatic persistence, and property-level change notifications.

### Architecture Overview

DroidNet.Config implements a layered architecture combining the .NET Options Pattern with additional capabilities for change tracking and persistence. The pattern consists of four key components:

#### 1. Settings POCO (Plain Old CLR Object)

The settings POCO is a simple data class used exclusively for binding configuration data from JSON files through the Microsoft.Extensions.Configuration framework.

**Characteristics:**

- Plain class with auto-properties
- No business logic or validation
- Directly deserializable from JSON
- Used internally by `IOptionsMonitor<T>`

**Example:**

```csharp
public class ThemeSettings
{
    public string Theme { get; set; } = "Light";
    public string AccentColor { get; set; } = "#0078D4";
}
```

#### 2. Settings Data Interface

The data interface provides a read-only contract for accessing settings values. This interface is what application code depends on.

**Characteristics:**

- Defines the data contract for settings access
- Contains only properties (no methods)
- Properties may be read-only (`{ get; }`) or read-write (`{ get; set; }`) depending on complexity
- Implemented by the settings service

**Simple Settings Example (Read-Write Properties):**

```csharp
public interface IAppearanceSettings
{
    ElementTheme AppThemeMode { get; set; }
    string AppThemeBackgroundColor { get; set; }
    string AppThemeFontFamily { get; set; }
}
```

**Complex Settings Example (Read-Only Properties):**

```csharp
public interface IWindowDecorationSettings
{
    IReadOnlyDictionary<string, WindowDecorationOptions> CategoryOverrides { get; }
}
```

#### 3. Settings Service Interface

The service interface extends `ISettingsService<TSettings>` and adds domain-specific operations for settings management.

**Characteristics:**

- Extends `ISettingsService<TSettings>` from DroidNet.Config
- Adds domain-specific methods for complex settings manipulation
- Provides validation and business logic
- Makes the service mockable for testing

**Example:**

```csharp
public interface IWindowDecorationSettingsService : ISettingsService<WindowDecorationSettings>
{
    IReadOnlyDictionary<string, WindowDecorationOptions> CategoryOverrides { get; }

    WindowDecorationOptions GetEffectiveDecoration(string category);
    void SetCategoryOverride(string category, WindowDecorationOptions options);
    void RemoveCategoryOverride(string category);
    Task SaveAsync();
}
```

#### 4. Settings Service Implementation

The service implementation extends `SettingsService<TSettings>` base class and implements the settings data interface. It must also be registered in the dependency injection container as `ISettingsService<TSettings>` to ensure proper singleton behavior and consistent access patterns.

**Characteristics:**

- Extends `SettingsService<TSettings>` base class
- **Must implement the settings data interface** (e.g., `IAppearanceSettings`)
- Receives `IOptionsMonitor<TSettings>` via dependency injection
- Provides `INotifyPropertyChanged` support from base class
- Tracks changes via `IsDirty` property
- **Automatic persistence**: Changes are automatically saved after a configurable delay (default 5 seconds)
- **Save-on-disposal**: Unsaved changes are persisted when the service is disposed
- Exposes typed settings access through the `Settings` property

**Example:**

```csharp
public class AppearanceSettingsService : SettingsService<AppearanceSettings>, IAppearanceSettings
{
    private ElementTheme appThemeMode;

    public AppearanceSettingsService(
        IOptionsMonitor<AppearanceSettings> settingsMonitor,
        IFileSystem fileSystem,
        ILoggerFactory? loggerFactory = null)
        : base(settingsMonitor, fileSystem, loggerFactory,
               autoSaveDelay: TimeSpan.FromSeconds(5))  // Optional
    {
        this.appThemeMode = settingsMonitor.CurrentValue.AppThemeMode;
    }

    public ElementTheme AppThemeMode
    {
        get => this.appThemeMode;
        set => this.SetField(ref this.appThemeMode, value);  // Auto dirty tracking
    }

    // Implement required abstract methods: UpdateProperties, GetConfigFilePath, etc.
}
```

### Usage Patterns

DroidNet.Config supports two patterns depending on settings complexity:

#### Pattern Selection Guide

| Pattern | Property Types | Interface Properties | Modification Method | Use Case |
|---------|---------------|---------------------|---------------------|----------|
| **Simple** | Scalar (string, int, enum) | Read-write | Direct property set | Theme, locale, simple preferences |
| **Complex** | Collections, validated | Read-only | Service methods | Window layouts, custom dictionaries |

#### Simple Settings: Direct Modification

For scalar properties, expose read-write properties on the data interface:

```csharp
public interface IAppearanceSettings
{
    ElementTheme AppThemeMode { get; set; }  // Direct modification
}

// Usage
settingsService.Settings.AppThemeMode = ElementTheme.Dark;
```

**Benefits**: Straightforward API, automatic change tracking, no validation overhead.

#### Complex Settings: Service-Mediated

For collections or validated data, expose read-only properties and provide service methods:

```csharp
public interface IWindowDecorationSettings
{
    IReadOnlyDictionary<string, WindowDecorationOptions> CategoryOverrides { get; }
}

public interface IWindowDecorationSettingsService : ISettingsService<...>
{
    void SetCategoryOverride(string category, WindowDecorationOptions options);
}
```

**Benefits**: Enforces validation, prevents invalid states, granular change tracking.

### Registration and Consumption

Proper registration and consumption patterns are critical for maintaining singleton behavior.

#### Registration Requirements

**Always register as `ISettingsService<TSettings>` interface:**

```csharp
// ✅ CORRECT
container.Register<ISettingsService<IAppearanceSettings>, AppearanceSettingsService>(Reuse.Singleton);

// ❌ WRONG: Dual registration creates multiple instances
container.Register<AppearanceSettingsService>(Reuse.Singleton);
container.Register<ISettingsService<IAppearanceSettings>, AppearanceSettingsService>(Reuse.Singleton);
```

**Critical**: Dual registration creates two separate instances—one for concrete type requests, one for interface requests. This breaks singleton behavior and causes synchronization bugs.

#### Consumption Pattern

**Always inject `ISettingsService<TSettings>` and use the `Settings` property:**

```csharp
public class ThemeManager
{
    private readonly ISettingsService<IAppearanceSettings> service;

    public ThemeManager(ISettingsService<IAppearanceSettings> service)
    {
        this.service = service;

        // Access settings via Settings property
        var theme = this.service.Settings.AppThemeMode;

        // Subscribe to changes
        this.service.PropertyChanged += (s, e) =>
        {
            if (e.PropertyName == nameof(IAppearanceSettings.AppThemeMode))
                ApplyTheme(this.service.Settings.AppThemeMode);
        };
    }
}
```

**Benefits**: Single instance across app, type-safe access, MVVM-compatible notifications, automatic persistence.

### Auto-Save Behavior

The base `SettingsService<TSettings>` class provides automatic persistence:

#### Debounced Saving

| Aspect | Default | Configurable |
|--------|---------|--------------|
| **Delay** | 5 seconds after last change | Pass `autoSaveDelay` to constructor |
| **Batching** | Multiple rapid changes → single save | Automatic |
| **Disable** | — | Pass `TimeSpan.Zero` |

```csharp
// Custom delay
: base(settingsMonitor, fileSystem, loggerFactory, autoSaveDelay: TimeSpan.FromSeconds(10))

// Disable (manual SaveSettings() required)
: base(settingsMonitor, fileSystem, loggerFactory, autoSaveDelay: TimeSpan.Zero)
```

#### Save-on-Disposal

When disposed, the service:

1. Stops auto-save subscription to prevent concurrent saves
2. Saves any `IsDirty` changes immediately
3. Logs a warning if unsaved changes exist

This ensures data safety even during unexpected application termination.

### Why Settings Services?

The .NET Options Pattern provides three interfaces for accessing configuration. While powerful for server-side scenarios, they have significant limitations for interactive UI applications:

#### Standard Options Interfaces Comparison

| Interface | Lifetime | Runtime Updates | UI Binding | Desktop UI Suitability |
|-----------|----------|-----------------|------------|----------------------|
| `IOptions<T>` | Singleton | ❌ No | ❌ No | ❌ Static snapshot only |
| `IOptionsSnapshot<T>` | Scoped | ✅ Yes | ❌ No | ❌ No scope concept in desktop |
| `IOptionsMonitor<T>` | Singleton | ✅ Yes | ❌ No | ⚠️ Verbose, callback-based |

**Key Limitations for UI Applications:**

- **`IOptions<T>`**: Configuration frozen at startup—unsuitable for any settings users can change
- **`IOptionsSnapshot<T>`**: Scoped lifetime designed for HTTP requests—desktop apps have no meaningful request scope
- **`IOptionsMonitor<T>`**: Supports runtime updates but requires verbose `monitor.CurrentValue.Property` syntax and callback-based notifications incompatible with MVVM data binding

#### Why Settings Services?

DroidNet.Config encapsulates `IOptionsMonitor<T>` complexity and adds essential UI features:

##### Comparison: IOptionsMonitor vs. Settings Service

| Feature | IOptionsMonitor | SettingsService |
|---------|-----------------|----------------|
| **Property Access** | `monitor.CurrentValue.Theme` | `service.Settings.Theme` |
| **Change Notifications** | `OnChange` callback | `INotifyPropertyChanged` |
| **UI Data Binding** | ❌ Not supported | ✅ MVVM compatible |
| **Dirty Tracking** | ❌ No | ✅ `IsDirty` property |
| **Auto-Save** | ❌ No | ✅ Configurable debounce |
| **Persistence** | ❌ Manual | ✅ Automatic + manual |

##### What Settings Services Add

1. **Configuration Binding**: Leverages `IOptionsMonitor<T>` for JSON-to-POCO binding
2. **Change Monitoring**: Subscribes to `IOptionsMonitor.OnChange` for external configuration updates
3. **Property Notifications**: `INotifyPropertyChanged` for MVVM data binding
4. **Automatic Persistence**: Debounced saves (default 5 seconds) reduce I/O
5. **Dirty Tracking**: `IsDirty` indicates unsaved changes
6. **Type-Safe API**: Clean `service.Settings.Property` access pattern

##### Architecture Benefits

| Benefit | Description |
|---------|-------------|
| **MVVM Compatibility** | Direct data binding support for WPF/WinUI/Avalonia |
| **Simplified API** | Hides `IOptionsMonitor<T>` verbosity |
| **Separation of Concerns** | POCO (binding) → Interface (contract) → Service (logic) |
| **Testability** | Mockable interfaces for unit tests |
| **Data Safety** | Auto-save on changes and disposal |
| **Framework Integration** | Works with ASP.NET Core and .NET Generic Host |

## Complete Integration Example

This example demonstrates integrating both PathFinder and Settings Services in a .NET Generic Host application.

### Configuration File

Settings are stored in JSON files located via PathFinder:

```json
{
  "AppearanceSettings": {
    "AppThemeMode": "Dark",
    "AccentColor": "#0078D4"
  }
}
```

### Host Setup

```csharp
public static partial class Program
{
    private static IPathFinder? PathFinderService { get; set; }
    private static IFileSystem FileSystemService { get; } = new RealFileSystem();

    private static void Main(string[] args)
    {
        var builder = Host.CreateDefaultBuilder(args)
            .UseServiceProviderFactory(new DryIocServiceProviderFactory(new Container()))
            .ConfigureAppConfiguration((_, config) =>
            {
                // Create PathFinder from command line args (--mode dev|real)
                var tempConfig = config.Build();
                PathFinderService = CreatePathFinder(tempConfig);
            })
            .ConfigureAppConfiguration(AddConfigurationFiles)
            .ConfigureServices(ConfigureOptionsPattern)
            .ConfigureContainer<DryIocServiceProvider>(
                provider => ConfigureEarlyServices(provider.Container));

        builder.Build().Run();
    }
}
```

### Configuration Steps

Key configuration methods:

**1. Add Configuration Files:**

```csharp
private static void AddConfigurationFiles(IConfigurationBuilder config)
{
    var configFile = PathFinderService.GetConfigFilePath("LocalSettings.json");
    config.AddJsonFile(configFile, optional: true, reloadOnChange: true);
}
```

**2. Configure Options Pattern:**

```csharp
private static void ConfigureOptionsPattern(HostBuilderContext context, IServiceCollection sc)
{
    sc.Configure<AppearanceSettings>(context.Configuration.GetSection(nameof(AppearanceSettings)));
}
```

**3. Register Early Services:**

```csharp
private static void ConfigureEarlyServices(IContainer container)
{
    container.RegisterInstance(FileSystemService);
    container.RegisterInstance(PathFinderService);
    // ... other services
}
```

**4. Register Settings Services:**

```csharp
private static void ConfigureServices(IContainer container)
{
    // Register settings services as interface only
    container.Register<ISettingsService<IAppearanceSettings>, AppearanceSettingsService>(Reuse.Singleton);
}
```

**Important**: PathFinder must be created before configuration files are added, then registered in the container for dependency injection.

### Application Code Example

Both services work together seamlessly:

```csharp
public class DocumentManager
{
    private readonly IPathFinder pathFinder;
    private readonly ISettingsService<IAppearanceSettings> appearanceSettings;

    public DocumentManager(
        IPathFinder pathFinder,
        ISettingsService<IAppearanceSettings> appearanceSettings)
    {
        this.pathFinder = pathFinder;
        this.appearanceSettings = appearanceSettings;
    }

    public void SaveDocument(Document doc)
    {
        // Use PathFinder to locate save directory
        var savePath = Path.Combine(pathFinder.UserDocuments, "MyApp", doc.FileName);

        // Use settings for user preferences
        var theme = appearanceSettings.Settings.AppThemeMode;
        doc.ApplyTheme(theme);
        doc.Save(savePath);
    }

    public void ChangeTheme(ElementTheme newTheme)
    {
        // Settings automatically persist after 5 seconds
        appearanceSettings.Settings.AppThemeMode = newTheme;
    }
}
```

**Key Points:**

- PathFinder provides environment-aware paths
- Settings Services provide type-safe, auto-persisting configuration
- Both services are mockable for unit testing
- Both support the same DI container patterns

## Testing

The Config project includes comprehensive unit tests with high code coverage. The tests use MSTest with FluentAssertions for readable assertions and Moq for mocking dependencies.

### Running Tests

Navigate to the Config solution directory and run:

```shell
dotnet test Config.sln
```

**Expected output:**

```text
Test summary: total: 45, failed: 0, succeeded: 45, skipped: 0
```

### Running Tests with Code Coverage

To generate code coverage reports:

```shell
dotnet test Config.sln -- --coverage
```

This generates a binary coverage file that can be opened in Visual Studio.

### Generating Cobertura Coverage Report

For CI/CD integration or viewing in VS Code, generate a Cobertura XML report:

```shell
dotnet test Config.sln -- --coverage --coverage-output-format cobertura --coverage-output coverage.cobertura.xml
```

**Coverage report location:**

The coverage file is generated in the test output directory:

```text
../../bin/Config.Tests/x64/Debug/net9.0-windows10.0.26100.0/TestResults/coverage.cobertura.xml
```

(Relative to the Config project directory)

### Viewing Coverage in VS Code

1. Install the **Coverage Gutters** extension
2. Run tests with Cobertura output (command above)
3. Open the coverage file or use the extension's "Watch" feature
4. Green/red indicators appear in the gutter showing covered/uncovered lines

### Generating HTML Coverage Reports

For a detailed HTML report, use ReportGenerator:

```shell
# Install ReportGenerator (one-time setup)
dotnet tool install -g dotnet-reportgenerator-globaltool

# Run tests with coverage (from Config project directory)
➜ dotnet test -- --coverage --results-directory ./Coverage --coverage-output-format cobertura --coverage-output coverage.cobertura.xml
dotnet test Config.sln -- --coverage --coverage-output-format cobertura --coverage-output coverage.cobertura.xml

# Generate HTML report
➜ reportgenerator -reports:".\Coverage\coverage.cobertura.xml" -targetdir:".\CoverageReport" -reporttypes:Htmlreportgenerator -reports:"..\..\bin\Config.Tests\x64\Debug\net9.0-windows10.0.26100.0\TestResults\coverage.cobertura.xml" -targetdir:".\TestResults\CoverageReport" -reporttypes:Html

# Open the report
start .\TestResults\CoverageReport\index.html
```

The HTML report will be generated in `./TestResults/CoverageReport/` within the Config project directory.
