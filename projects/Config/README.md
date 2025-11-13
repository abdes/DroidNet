# DroidNet.Config

The **DroidNet.Config** module provides two essential services for application configuration management:

1. **PathFinder**: Environment-aware path resolution for system, user, and application directories
2. **Settings Services**: MVVM-friendly, DI-integrated settings services with change tracking, validation and durable
   persistence

Built on [DryIoc](https://github.com/dadhi/DryIoc) for dependency injection. Persistence and runtime loading are handled
by the module's own `SettingsManager` and concrete settings services; this project does not depend on the Microsoft
Options pattern (`IOptions*`).

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
    - [Usage patterns and canonical access](#usage-patterns-and-canonical-access)
  - [Registration and Consumption](#registration-and-consumption)
    - [Persistence](#persistence)
  - [Why Settings Services?](#why-settings-services)
- [Complete Integration Example (end-to-end)](#complete-integration-example-end-to-end)
- [Testing](#testing)
  - [Running Tests](#running-tests)
  - [Running Tests with Code Coverage](#running-tests-with-code-coverage)
  - [Viewing Coverage in VS Code](#viewing-coverage-in-vs-code)

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
- **Type-Safe Access**: Clean `service.Property` API pattern
- **Framework Integration**: DI-friendly and designed for desktop/interactive usage; persistence is handled by the
  module's `SettingsManager`

## Getting Started

```shell
dotnet add package DroidNet.Config
```

## PathFinder

The `PathFinder` service provides environment-aware path resolution, making it easy to locate system and application
directories consistently across development and production environments.

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

Settings Services provide a small runtime layer focused on UI-friendly features like MVVM data binding, explicit
persistence via `SettingsManager`, and property-level change notifications.

### Architecture Overview

The Config module provides a small, opinionated runtime layer that adds MVVM-friendly access, change-tracking,
validation and durable persistence for desktop/interactive applications. At its core are strongly-typed "settings
services" that implement the settings data interface (the service instance itself exposes the typed settings
properties), surface an `INotifyPropertyChanged`-compatible API for UI binding, track unsaved changes via an `IsDirty`
flag, support validation, and coordinate saving through the module's `SettingsManager`.

The `SettingsManager` provides optional auto-save capabilities (controlled via `AutoSave` and `AutoSaveDelay`). When
enabled, an internal debounced `AutoSaver` monitors services for `IsDirty` changes and will automatically call
`SaveAsync` after a quiet period (default delay: 2 seconds). Toggling `AutoSave` off will cause the manager to attempt
an immediate save of any dirty services before stopping the auto-save worker.

The base service implements locking, dirty-tracking suppression for bulk updates, and a POCO snapshot pattern for
serialization. Concrete services must implement the settings data interface, provide snapshot and update logic, and
should be registered and consumed as `ISettingsService<TSettings>` to preserve singleton behavior and correct wiring.

#### 1. Settings POCO (Plain Old CLR Object)

The settings POCO is a simple data class used as a serialization model for persisted settings (for example JSON). It
contains only data (auto-properties), no business logic. Concrete services create POCO snapshots using
`GetSettingsSnapshot()` for persistence and restore state using `UpdateProperties(TSettings)`.

**Characteristics:**

- Plain class with auto-properties
- No business logic or validation (validation is performed by the service)
- Used as the serialization/deserialize model when saving/loading via `SettingsManager`

**Example:**

```csharp
public class ThemeSettings
{
    public string Theme { get; set; } = "Light";
    public string AccentColor { get; set; } = "#0078D4";
}
```

#### 2. Settings Data Interface

The data interface provides a read-only contract for accessing settings values.
This interface is what application code depends on.

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

The service implementation extends `SettingsService<TSettings>` base class and implements the settings data interface.
It must also be registered in the dependency injection container as `ISettingsService<TSettings>` to ensure proper
singleton behavior and consistent access patterns.

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
        SettingsManager manager,
        ILoggerFactory? loggerFactory = null)
        : base(manager, loggerFactory)
    {
        // Initialize fields from defaults or by loading via SettingsManager in your implementation.
    }

    public ElementTheme AppThemeMode
    {
        get => this.appThemeMode;
        set => this.SetField(ref this.appThemeMode, value);  // Auto dirty tracking
    }

    // Implement required abstract methods: UpdateProperties, GetConfigFilePath, etc.
}
```

### Usage patterns and canonical access

There is one canonical, consistent model for accessing and mutating settings: inject `ISettingsService<TSettings>`.
Concrete service types implement the `TSettings` interface, so the service instance itself exposes the typed
properties and can be used directly by application code and the UI (for example: `service.SomeProperty = value`).

### Registration and Consumption

Use the provided bootstrapper/DI helpers in `BootstrapperExtensions.cs` to register the config infrastructure,
sources and strongly-typed services. The helpers wire `SettingsManager` and settings services correctly so that
consumers always resolve a single instance via `ISettingsService<TSettings>`.

Example (fluent registration via the bootstrapper pattern):

```csharp
var container = new Container()
        .WithConfig() // registers SettingsManager and core infrastructure
        .WithJsonConfigSource(id: "localsettings", filePath: "path/to/LocalSettings.json", watch: true)
        .WithSettings<IAppearanceSettings, AppearanceSettingsService>();
```

#### Notes

- `WithConfig()` registers `SettingsManager` as a singleton and maps the manager interface to the concrete.
- `WithJsonConfigSource(...)` registers an `ISettingsSource` keyed by the provided id. If you specify an encryption
  provider type, that type must implement `IEncryptionProvider` and be resolvable from the container (register the
  concrete encryption provider before calling `WithJsonConfigSource`).
- `WithSettings<TSettingsInterface, TService>()` registers the concrete service in a way that the `SettingsManager` can
  initialize it and then registers a delegate so consumers receive the manager-backed singleton when they resolve
  `ISettingsService<TSettingsInterface>`.

Why this matters

The POCO type (the plain settings class used for serialization) is strictly an internal serialization model. Do
not register, inject, or otherwise depend on the POCO or its concrete CLR type in application code. The POCO
exists only so the manager and sources can serialize/deserialize settings payloads.

```csharp
// Inject the manager-backed service (the service implements IAppearanceSettings)
public class ThemeManager
{
    private readonly ISettingsService<IAppearanceSettings> service;

    public ThemeManager(ISettingsService<IAppearanceSettings> service)
    {
        this.service = service;
    }

    public void SetTheme(ElementTheme theme)
    {
        this.service.AppThemeMode = theme; // canonical access (service implements IAppearanceSettings)
    }
}
```

Why: registering or injecting the POCO or the concrete service type bypasses the `SettingsManager` initialization
and lifecycle, and results in multiple instances, lost synchronization and broken persistence behavior. Always
consume settings via `ISettingsService<TSettingsInterface>`; the bootstrapper helpers ensure consumers receive the
manager-backed singleton.

### Persistence

The base `SettingsService<TSettings>` provides explicit persistence APIs while `SettingsManager` offers an optional
auto-save facility for convenience. Key behaviors:

- `SaveAsync(CancellationToken)` validates the current snapshot and delegates to `SettingsManager` to persist data.
- `ValidateAsync(CancellationToken)` runs DataAnnotations validation against the POCO snapshot and returns
    `SettingsValidationError` entries when validation fails.
- `SaveAsync` compares the saved snapshot with the in-memory snapshot and only clears `IsDirty` when they match.

AutoSave (via `SettingsManager.AutoSave`) is optional and debounced. The manager's default `AutoSaveDelay` is
2 seconds. The internal `AutoSaver` subscribes to `PropertyChanged` on services and debounces saves (it also
handles concurrent save requests and pending saves). Turning AutoSave off will attempt to save all dirty services
immediately before stopping the background worker.

Encryption: sources may optionally use a pluggable `IEncryptionProvider` implementation to encrypt and decrypt
binary blobs stored by a source (e.g., JSON payloads or binary archives). Implement `IEncryptionProvider` to
provide encryption/decryption primitives; the interface is algorithm-agnostic and expects implementations to
handle IV/nonce management and AEAD authentication where appropriate.

### Why Settings Services?

This module provides a small, focused infrastructure for runtime settings in interactive applications. Its key
differentiators are:

1. MVVM compatibility via `INotifyPropertyChanged` so settings can be bound directly from UI views.
2. Explicit change tracking (`IsDirty`) and controlled persistence via `SaveAsync` + `SettingsManager`.
3. Built-in validation support (`ValidateAsync`) using DataAnnotations.
4. A snapshot and restore pattern (POCO snapshots + `UpdateProperties`) that separates serialization from runtime state.

These features make the services easier to use in desktop apps where UI binding, validation feedback, and
explicit persistence are primary concerns.

## Complete Integration Example (end-to-end)

This section shows a minimal, end-to-end console example that matches the `Program.cs` sample in the project. It wires
the bootstrapper, registers the Config module, adds JSON sources, registers the real file system for production use,
initializes the `SettingsManager`, and runs a small interactive loop to demonstrate reading and mutating settings.

The provided console program expects a folder path that contains JSON files (for example `settings.json`,
`settings.dev.json`, and `settings.user.json`). The sample program from `Program.cs` uses the `Bootstrapper` helpers to
register the Config services and demonstrate interactive commands such as `show`, `toggle`, `setname`, `autosave`,
`autodelay`, `save`, and `reload`.

### Sample configuration JSON

Place JSON files under a folder (for example `samples/`) passed to the program. Example `settings.json`:

```json
{
  "AppSettings": {
    "ApplicationName": "MyApp",
    "LoggingLevel": "Information",
    "EnableExperimental": false
  }
}
```

Optional environment-specific overrides (e.g., `settings.dev.json` or `settings.real.json`) can be added. A user-level
file `settings.user.json` may contain personal overrides.

### What the sample program does (high level)

- Validates command line arguments and expects the path to a folder containing the JSON configuration files.
- Creates a `Bootstrapper` which exposes a DryIoc `Container` and helper registration methods.
- Registers `System.IO.Abstractions.IFileSystem` with a concrete `RealFileSystem` so the Config sources read/write the
  real disk.
- Registers the Config module via `WithConfig()` and adds JSON config sources via `WithJsonConfigSource(...)` for
  `base`, `dev` (based on PathFinder.Mode) and `user`.
- Registers a typed settings service `ISettingsService<IAppSettings>` backed by `AppSettingsService` via the
  bootstrapper helper `WithSettings`.
- Calls `bootstrap.Build()` and resolves `SettingsManager` to call `InitializeAsync()` so all sources and services are
  loaded and ready.
- Runs an interactive REPL where the user can inspect and change settings. Changes are persisted via `SaveAsync()` or by
  the manager's auto-save.

### Important wiring notes (from Program.cs)

- The sample registers the real filesystem:

  - `container.RegisterInstance<System.IO.Abstractions.IFileSystem>(new RealFileSystem());`

- Config sources are added with file paths relative to the provided samples folder. The sample adds sources like:

  - `WithJsonConfigSource("base", Path.Combine(samplesPath, "settings.json"), watch: true)`
  - `WithJsonConfigSource("dev", Path.Combine(samplesPath, $"settings.{pathFinder.Mode}.json"), watch: true)`
  - `WithJsonConfigSource("user", Path.Combine(samplesPath, "settings.user.json"), watch: true)`

- The settings service is registered via `WithSettings<IAppSettings, AppSettingsService>()` so consumers resolve
  `ISettingsService<IAppSettings>` to access typed properties.

### Interactive commands (supported by the sample)

- `show` — prints current settings values
- `toggle` — toggles a boolean setting (example: `EnableExperimental`)
- `setname <name>` — sets `ApplicationName`
- `autosave [on|off|toggle|status]` — control auto-save behavior on the `SettingsManager`
- `autodelay <seconds>` — set the auto-save debounce delay
- `save` — explicitly save current settings
- `reload` — reload all configuration sources
- `exit` — quit the program

### Try it (Windows PowerShell)

```powershell
dotnet run --project samples\DroidNet.Config.Example.csproj "$($PWD.Path)\samples" --mode dev
```

## Testing

The Config project includes comprehensive unit tests with high code coverage. The tests use MSTest with AwesomeAssertions
for readable assertions and Moq for mocking dependencies.

### Running Tests

Navigate to the Config solution directory and run:

```shell
dotnet test Config.sln
```

### Running Tests with Code Coverage

Use the provided `cover.ps1` helper script to run tests with coverage and generate an HTML report (recommended):

From the `projects\Config` directory run:

```powershell
# Get Help
Get-Help .\cover.ps1

# Run tests and produce Cobertura XML (no HTML unless reportgenerator is installed)
.\cover.ps1

# Run and install ReportGenerator if needed, then generate and open HTML report
.\cover.ps1 -InstallReportGenerator

# Run with Release configuration
.\cover.ps1 -Configuration Release
```

The script will place intermediate results in `./TestResults` under the `projects\Config` folder and will generate an HTML report in `./TestResults/CoverageReport/index.html` when ReportGenerator is available.

If you prefer the raw dotnet commands, the equivalent manual steps are:

```powershell
# Run tests with coverage (Cobertura XML)
dotnet test Config.sln -- --coverage --coverage-output-format cobertura --coverage-output coverage.cobertura.xml

# (optional) Generate HTML report with ReportGenerator (install first if needed)
dotnet tool install -g dotnet-reportgenerator-globaltool
reportgenerator -reports:"<path-to-coverage.cobertura.xml>" -targetdir:"<target-html-folder>" -reporttypes:Html
```

The README above shows the typical coverage XML location produced by the tests (relative to the Config project):

```text
../../bin/Config.Tests/x64/Debug/net9.0-windows10.0.26100.0/TestResults/coverage.cobertura.xml
```

### Viewing Coverage in VS Code

1. Install the **Coverage Gutters** extension.
2. Run `cover.ps1` (or the dotnet command) to produce a Cobertura XML file.
3. Open the generated Cobertura file or use the extension's "Watch" feature to show coverage in the editor gutter.

## Missing Features

- GOAL-004: Implement secure storage for sensitive configuration data using encryption

| Completed | Task | Description |
|------|-------------|-----------|
|✅| TASK-021 | Create `EncryptedJsonSettingsSource` class in `src/Sources/EncryptedJsonSettingsSource.cs` extending JsonSettingsSource |
|✅| TASK-022 | Implement Secret&lt;T&gt; encryption/decryption using platform-appropriate APIs (DPAPI on Windows) |
| | TASK-023 | Add key management and rotation capabilities to EncryptedJsonSettingsSource |
| | TASK-024 | Implement secure memory handling to prevent secret leakage in logs or exceptions |
| | TASK-025 | Add validation to prevent Secret&lt;T&gt; properties from being saved to non-encrypted sources |
