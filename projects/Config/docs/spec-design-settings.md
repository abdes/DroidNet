# Settings Module Design Specification

This specification defines the requirements, constraints, interfaces, data
contracts, and acceptance criteria for the revamped configuration/settings
module ("Settings Module") used across the DroidNet solutions. It is written to
be machine- and AI-friendly and self-contained so it can be used by engineers
and generative tools to implement, test, and validate the design.

## 1. Purpose

### Primary Objective

Provide a single, well-defined, type-safe, and testable configuration/settings
subsystem that supports application-wide settings, runtime notifications,
persistence, validation, and secure secrets handling.

### Scope & Coverage

The Settings Module covers:

- Public APIs for reading, writing, subscribing to, and validating settings
- Durable storage backends (file-based JSON, optional encrypted store) with pluggable abstraction
- Multi-source settings composition with last-loaded-wins strategy
- Type-safe settings access via strongly-typed POCOs
- Unit and integration test contracts

- Runtime configuration hot-reloading (with source change notifications)

### Exclusions

- UI controls or editor surfaces for settings (they consume the Settings Module through public APIs)
- External configuration sources (e.g., cloud key vaults) except via pluggable settings source interface
- Settings validation UI/user feedback (validation errors are surfaced via exceptions)

### Target Audience

- **Primary**: Developers implementing the Settings Module
- **Secondary**: Consumer developers who will depend on the public APIs
- **Tertiary**: QA engineers and CI/CD automation systems

### Assumptions & Prerequisites

- .NET ecosystem (C# language, .NET 8+ runtime per global.json)
- Dependency injection container (DryIoc) for service registration
- Async/await patterns for I/O operations
- System.Text.Json for JSON serialization (no Newtonsoft.Json dependency)

## 2. Definitions

- **Setting**: A named piece of configuration data (scalar, list, or object) consumed by application code. May have
  custom converters for serialization/deserialization. Individual validation of settings is the responsibility of the
  `Settings Service`
- **TSettings**: The data contract of a collection of structured settings. An interface, implemented by a POCO, which
  will be the medium for serialization / deserialization, and by the corresponding settings service. For use only by the
  serilization layer and the `Settings Service`.
- **Settings Service**: A typed service (`ISettingsService<TSettings>`) that provides access to a specific settings
  **section/schema** with type-safe property access, and property change notifications. Created and managed by the
  `SettingsManager` and can be injected via DI wherever needed. Access to settings is done via the service property
  `Settings` of type `TSettings`.
- **Settings Source**: A pluggable component, focusing on persistence and serialization / deserialization of settings.
  Consumes serilaized settings (text) from a persistent store (such as file, encrypted file) and produces structured
  deserialized settings as POCO objects (see `TSettings`). Example of such component is a JsonSettingsFile, which would
  use the `System.Text.Json` for serialization /deserialization, and a file store for persistence. Contains multiple
  sections, including "metadata" section and zero or many sections handled by corresponding `Settings Service`s. There
  should be no items at the root level other than sections. Any root level items are discarded.
- **Settings Manager**: The central orchestrating service that manages all settings sources and provides
  `ISettingsService<TSettings>` instances. Registered in DI, and injected with all registered `SettingSource`s.
- **Secret**: Sensitive configuration value that must be stored encrypted-at-rest and handled securely in memory
- **Atomic Write**: A write operation that either completely succeeds or completely fails, preventing partial/corrupted state

## 3. Requirements, Constraints & Guidelines

### Functional Requirements

- **REQ-001**: Provide a single public service interface for runtime use for any `TSettings` type via its corresponding `ISettingsService<TSettings>`, which is DI injectable.
- **REQ-002**: Support typed settings access, mapping to POCOs, with compile-time type safety
- **REQ-003**: Support multiple settings sources. There is no notion of precedence order for value resolution. Last loaded source is the winner.
- **REQ-004**: Persist settings to durable storage with atomic writes to prevent corruption (file writes must be atomic/replace-on-write)
- **REQ-005**: Provide property-level change notifications via `INotifyPropertyChanged` on the `ISettingsService<TSettings>` implementation
- **REQ-006**: Provide source-level lifecycle notifications (source added/updated/removed) via C# event mechanism
- **REQ-007**: Propagate updates to settings services when sources are reloaded or modified externally, without creating a new instance of the service (no disruption to previously injected consumers)
- **REQ-008**: Validate settings using source-level (e.g. JSON schema,) when applicable/supported
- **REQ-009**: Validate settings at the property level via `Settings Service` implementations
- **REQ-010**: Handle secrets securely with encryption-at-rest for sources that support it

### Non-Functional Requirements

- **REQ-012**: Ensure thread-safety with concurrent reads and serialized writes
- **REQ-013**: Use asynchronous I/O APIs throughout; no synchronous file operations in public contracts
- **REQ-014**: Implement graceful error handling - source failures must not crash the host application
- **REQ-015**: Support cancellation tokens for all async operations to enable timeout and cancellation scenarios

### Settings Source Requirements

- **REQ-016**: Settings sources must focus on file/text I/O: read and write raw text content at specified paths
- **REQ-017**: Settings sources must implement atomic write semantics (write to temporary file, then atomic rename/replace)
- **REQ-018**: Settings sources may perform file-level or schema validation with clear, descriptive error messages
- **REQ-019**: Settings sources must not perform property-level typed validation (responsibility of the service layer)
- **REQ-020**: Settings sources may optionally support encrypted content
- **REQ-021**: Settings sources must provide metadata and serialized settings content, in loading mode
- **REQ-022**: Settings sources must serialize settings content, and when successful, write metadata and serialized content, in saving mode
- **REQ-023**: Settings sources must not persist invalid data

### Service Layer Requirements

- **REQ-024**: The `ISettingsService<TSettings>` implementation must implement the `TSettings` interface
- **REQ-025**: The service must provide property change notifications for its settings properties
- **REQ-026**: The service must perform property-level validation (types, ranges, required fields) and surface `SettingsValidationException` on failures

### Constraints

- **CON-001**: Must not hard-code package/library versions in public contracts
- **CON-002**: Must use System.Text.Json for JSON operations (no Newtonsoft.Json dependency)
- **CON-003**: Must integrate with existing DryIoc dependency injection container
- **CON-004**: Must follow existing project coding standards and architectural patterns

### Design Guidelines

- **GUD-001**: Use interface segregation - consumers should only depend on interfaces they actually use
- **GUD-002**: Keep public contracts minimal and stable; prefer extension points via settings sources and events
- **GUD-003**: Prefer composition over inheritance
- **GUD-004**: Use `Testably` for testing without actual files

## 4. Architecture Overview

### Component Responsibilities

#### SettingsManager

The **SettingsManager** is the central orchestrator that:

- Manages the lifecycle of all registered settings sources
- Coordinates source loading (last loaded source wins, no precedence)
- Provides factory methods to create `ISettingsService<TSettings>` instances
- Handles source-level events and notifications

#### ISettingsService&lt;TSettings&gt;

Each **ISettingsService&lt;TSettings&gt;** instance:

- Exposes a `Settings` property that implements the `TSettings` interface
- Provides typed access to settings properties via the Settings property
- Handles property-level validation and change notifications
- Manages persistence of its settings via the SettingsManager
- Acts as the primary interface for consumers working with settings

#### ISettingsSource

Each **ISettingsSource** implementation:

- Handles persistence (file I/O) for a specific storage backend
- Performs serialization/deserialization of settings to/from text format
- Implements atomic write semantics and file watching
- Provides source-specific capabilities (encryption, read-only, etc.)
- Returns both metadata and deserialized settings objects

### Data Flow Architecture

```text
Consumer Code
    ↓
ISettingsService<IEditorSettings>     ISettingsService<IAppSettings>
(.Settings: IEditorSettings)          (.Settings: IAppSettings)
    ↓                                    ↓
         SettingsManager (Central Orchestrator)
                    ↓
    ┌──────────────┼──────────────┬──────────────┐
    ↓              ↓              ↓              ↓
JsonSource    EncryptedSource  EmbeddedSource  EnvSource
(serializes   (serializes      (serializes     (serializes
& persists)   & encrypts)      defaults)       env vars)
```

### Source Loading & Merging

1. SettingsManager loads all sources in registration order
2. Each source performs its own deserialization from persistent store to POCO objects
3. SettingsManager applies "last loaded wins" strategy (no precedence)
4. ISettingsService&lt;TSettings&gt; instances receive the final merged settings
5. Each service exposes settings via the Settings property that implements TSettings interface

## 5. Interfaces & Data Contracts

```csharp
/// <summary>
/// The primary contract for strongly-typed settings service used at runtime.
/// This is the single canonical service contract consumers should use.
/// The service implementation must implement the TSettings interface for direct property access.
/// </summary>
/// <typeparam name="TSettings">The strongly-typed settings interface</typeparam>
public interface ISettingsService<TSettings> : INotifyPropertyChanged, IDisposable
    where TSettings : class, new()
{
    /// <summary>
    /// The settings object that implements the TSettings interface.
    /// This is the primary way consumers access settings properties.
    /// </summary>
    TSettings Settings { get; }

    /// <summary>
    /// True when the in-memory settings have local changes not yet persisted to sources.
    /// </summary>
    bool IsDirty { get; }

    /// <summary>
    /// True when the service has been initialized and settings are available.
    /// </summary>
    bool IsInitialized { get; }

    /// <summary>
    /// Initialize the service by loading from all configured sources.
    /// Must be called before accessing settings properties or using other methods.
    /// </summary>
    /// <param name="ct">Cancellation token</param>
    /// <returns>Task that completes when initialization is finished</returns>
    Task InitializeAsync(CancellationToken ct = default);

    /// <summary>
    /// Reload settings from all configured sources, applying last-loaded-wins strategy.
    /// Triggers PropertyChanged notifications for any changed values.
    /// </summary>
    /// <param name="ct">Cancellation token</param>
    /// <returns>Task that completes when reload is finished</returns>
    Task ReloadAsync(CancellationToken ct = default);

    /// <summary>
    /// Persist the current in-memory settings to the writable source.
    /// Performs property-level validation and throws SettingsValidationException on validation failures.
    /// Uses atomic write semantics for file-backed sources.
    /// </summary>
    /// <param name="ct">Cancellation token</param>
    /// <returns>Task that completes when save is finished</returns>
    /// <exception cref="SettingsValidationException">Thrown when validation fails</exception>
    /// <exception cref="SettingsPersistenceException">Thrown when persistence fails</exception>
    Task SaveAsync(CancellationToken ct = default);

    /// <summary>
    /// Subscribe to settings source lifecycle events (source added/updated/removed/failed).
    /// Useful for components that need to react to source-level changes.
    /// </summary>
    /// <param name="handler">Event handler for source changes</param>
    /// <returns>Disposable subscription that can be disposed to unsubscribe</returns>
    IDisposable SubscribeToSourceChanges(Action<SettingsSourceChangedEventArgs> handler);



    /// <summary>
    /// Reset settings to default values as defined by the TSettings interface.
    /// Does not persist changes until SaveAsync is called.
    /// </summary>
    void ResetToDefaults();

    /// <summary>
    /// Validate the current settings without persisting.
    /// Useful for pre-validation before SaveAsync.
    /// </summary>
    /// <returns>Validation results; empty if valid</returns>
    Task<IReadOnlyList<SettingsValidationError>> ValidateAsync(CancellationToken ct = default);
}

/// <summary>
/// Types of changes that can occur to settings sources.
/// </summary>
public enum SettingsSourceChangeType
{
    /// <summary>A new source was added to the service</summary>
    Added,
    /// <summary>An existing source was updated/reloaded</summary>
    Updated,
    /// <summary>A source was removed from the service</summary>
    Removed,
    /// <summary>A source failed to load or save</summary>
    Failed
}

/// <summary>
/// Event arguments for settings source lifecycle changes.
/// </summary>
public sealed class SettingsSourceChangedEventArgs
{
    /// <summary>
    /// Unique identifier for the settings source (typically file path or source name).
    /// </summary>
    public required string SourceId { get; init; }

    /// <summary>
    /// The type of change that occurred to the source.
    /// </summary>
    public required SettingsSourceChangeType ChangeType { get; init; }

    /// <summary>
    /// Optional details about the source or change (e.g., error message for Failed, file path, etc.).
    /// </summary>
    public string? Details { get; init; }

    /// <summary>
    /// Timestamp when the change occurred.
    /// </summary>
    public DateTimeOffset Timestamp { get; init; } = DateTimeOffset.UtcNow;
}

/// <summary>
/// Represents a validation error for a specific settings property.
/// </summary>
public sealed class SettingsValidationError
{
    /// <summary>
    /// The property path that failed validation (e.g., "Font.Size", "Window.Position.X").
    /// </summary>
    public required string PropertyPath { get; init; }

    /// <summary>
    /// Human-readable error message describing the validation failure.
    /// </summary>
    public required string Message { get; init; }

    /// <summary>
    /// The invalid value that caused the validation failure.
    /// </summary>
    public object? InvalidValue { get; init; }

    /// <summary>
    /// Optional error code for programmatic handling.
    /// </summary>
    public string? ErrorCode { get; init; }
}

/// <summary>
/// Exception thrown when settings validation fails.
/// </summary>
public sealed class SettingsValidationException : Exception
{
    /// <summary>
    /// The validation errors that caused this exception.
    /// </summary>
    public IReadOnlyList<SettingsValidationError> ValidationErrors { get; }

    public SettingsValidationException(IReadOnlyList<SettingsValidationError> validationErrors)
        : base($"Settings validation failed with {validationErrors.Count} error(s)")
    {
        ValidationErrors = validationErrors;
    }

    public SettingsValidationException(string message, IReadOnlyList<SettingsValidationError> validationErrors)
        : base(message)
    {
        ValidationErrors = validationErrors;
    }
}

/// <summary>
/// Exception thrown when settings persistence operations fail.
/// </summary>
public sealed class SettingsPersistenceException : Exception
{
    /// <summary>
    /// The source ID that failed to persist.
    /// </summary>
    public string? SourceId { get; }

    public SettingsPersistenceException(string message, string? sourceId = null)
        : base(message)
    {
        SourceId = sourceId;
    }

    public SettingsPersistenceException(string message, Exception innerException, string? sourceId = null)
        : base(message, innerException)
    {
        SourceId = sourceId;
    }
}

/// <summary>
/// Abstraction for settings sources that handle persistence and serialization/deserialization.
/// Sources are responsible for I/O operations, serialization, and providing structured settings data.
/// </summary>
public interface ISettingsSource
{
    /// <summary>
    /// Unique identifier for this source (typically file path or descriptive name).
    /// </summary>
    string SourceId { get; }

    /// <summary>
    /// Indicates whether this source supports write operations.
    /// Read-only sources (e.g., embedded defaults) return false.
    /// </summary>
    bool CanWrite { get; }

    /// <summary>
    /// Indicates whether this source can store encrypted secrets.
    /// </summary>
    bool SupportsSecrets { get; }

    /// <summary>
    /// Load settings from the persistent store and deserialize to structured data.
    /// Sources perform their own serialization/deserialization.
    /// </summary>
    /// <param name="ct">Cancellation token</param>
    /// <returns>Read result with metadata and deserialized settings data</returns>
    Task<SettingsSourceReadResult> LoadAsync(CancellationToken ct = default);

    /// <summary>
    /// Serialize settings data and save to the persistent store atomically.
    /// Must implement atomic write semantics (temp file + rename) to prevent corruption.
    /// </summary>
    /// <param name="settingsData">The structured settings data to serialize and save</param>
    /// <param name="metadata">Metadata to include with the settings</param>
    /// <param name="ct">Cancellation token</param>
    /// <returns>Write result with new version and status</returns>
    Task<SettingsSourceWriteResult> SaveAsync(object settingsData, SettingsMetadata metadata, CancellationToken ct = default);

    /// <summary>
    /// Check if this source exists and is accessible.
    /// </summary>
    /// <param name="ct">Cancellation token</param>
    /// <returns>True if the source exists and can be read</returns>
    Task<bool> ExistsAsync(CancellationToken ct = default);

    /// <summary>
    /// Delete/reset this settings source if supported.
    /// Not all sources support deletion (e.g., embedded defaults).
    /// </summary>
    /// <param name="ct">Cancellation token</param>
    /// <returns>Operation result</returns>
    Task<SettingsSourceResult> DeleteAsync(CancellationToken ct = default);

    /// <summary>
    /// Watch for external changes to this source (e.g., file system changes).
    /// Returns a disposable that stops watching when disposed.
    /// </summary>
    /// <param name="changeHandler">Handler called when the source changes externally</param>
    /// <returns>Disposable to stop watching, or null if watching is not supported</returns>
    IDisposable? WatchForChanges(Action<string> changeHandler);
}

/// <summary>
/// Metadata associated with settings content.
/// </summary>
public sealed class SettingsMetadata
{
    /// <summary>
    /// Settings content version for migration purposes.
    /// </summary>
    public required string Version { get; init; }

    /// <summary>
    /// Settings schema version (typically date-based).
    /// </summary>
    public required string SchemaVersion { get; init; }

    /// <summary>
    /// Identifier for the service/application.
    /// </summary>
    public string? Service { get; init; }

    /// <summary>
    /// ISO 8601 timestamp when settings were last written.
    /// </summary>
    public DateTimeOffset? WrittenAt { get; init; }

    /// <summary>
    /// Tool/component that wrote the settings.
    /// </summary>
    public string? WrittenBy { get; init; }
}

/// <summary>
/// Result of loading from a settings source.
/// </summary>
public sealed record SettingsSourceReadResult
{
    public required bool Success { get; init; }

    /// <summary>
    /// The deserialized settings data, if successful.
    /// </summary>
    public object? SettingsData { get; init; }

    /// <summary>
    /// Metadata associated with the settings.
    /// </summary>
    public SettingsMetadata? Metadata { get; init; }

    public string? ErrorMessage { get; init; }
    public Exception? Exception { get; init; }

    public static SettingsSourceReadResult CreateSuccess(object settingsData, SettingsMetadata? metadata = null) =>
        new() { Success = true, SettingsData = settingsData, Metadata = metadata };

    public static SettingsSourceReadResult CreateFailure(string errorMessage, Exception? exception = null) =>
        new() { Success = false, ErrorMessage = errorMessage, Exception = exception };
}

/// <summary>
/// Result of saving to a settings source.
/// </summary>
public sealed record SettingsSourceWriteResult
{
    public required bool Success { get; init; }

    /// <summary>
    /// New version identifier assigned after successful save.
    /// </summary>
    public string? NewVersion { get; init; }

    public string? ErrorMessage { get; init; }
    public Exception? Exception { get; init; }

    /// <summary>
    /// True if the operation failed due to concurrent modification.
    /// </summary>
    public bool WasConflict { get; init; }

    public static SettingsSourceWriteResult CreateSuccess(string? newVersion = null) =>
        new() { Success = true, NewVersion = newVersion };

    public static SettingsSourceWriteResult CreateFailure(string errorMessage, Exception? exception = null, bool wasConflict = false) =>
        new() { Success = false, ErrorMessage = errorMessage, Exception = exception, WasConflict = wasConflict };
}

/// <summary>
/// General result for settings source operations.
/// </summary>
public sealed record SettingsSourceResult
{
    public required bool Success { get; init; }
    public string? ErrorMessage { get; init; }
    public Exception? Exception { get; init; }

    public static SettingsSourceResult CreateSuccess() =>
        new() { Success = true };

    public static SettingsSourceResult CreateFailure(string errorMessage, Exception? exception = null) =>
        new() { Success = false, ErrorMessage = errorMessage, Exception = exception };
}
```

### Data Contract for Persisted Settings

#### JSON File Format Requirements

Each settings file MUST follow this structure:

1. **Top-level `metadata` object** containing version and optional tracking information
2. **Settings content** either as direct properties or under a named section
3. **Valid JSON format** compatible with System.Text.Json (no comments, trailing commas, etc.)

#### Standard JSON Structure

```json
{
  "metadata": {
    "version": "1.0.0",
    "schemaVersion": "2025-10-18",
    "service": "com.droidnet.editor",
    "writtenAt": "2025-10-18T12:34:56.789Z",
    "writtenBy": "SettingsManager/1.0"
  },
  "settings": {
    "font": {
      "family": "Consolas",
      "size": 14,
      "weight": "normal"
    },
    "window": {
      "size": { "width": 1600, "height": 900 },
      "position": { "x": 100, "y": 100 },
      "isMaximized": false
    },
    "features": {
      "experimentalFeatures": true,
      "telemetryEnabled": false
    },
    "secrets": {
      "apiKey": {
        "$type": "Secret",
        "value": "encrypted_base64_content_here"
      }
    }
  }
}
```

#### Alternative Flat Structure (for simple settings)

```json
{
  "metadata": {
    "version": "1.0.0",
    "schemaVersion": "2025-10-18"
  },
  "fontSize": 14,
  "fontFamily": "Consolas",
  "windowWidth": 1600,
  "windowHeight": 900
}
```

#### Metadata Object Requirements

| Field | Required | Type | Description |
|-------|----------|------|-------------|
| `version` | Yes | string | Settings content version |
| `schemaVersion` | Yes | string | Settings schema version (typically date-based) |
| `service` | No | string | Identifier for the SettingsService that manages this section of settings source (Fully Qualified Type) |
| `writtenAt` | No | string | ISO 8601 timestamp when file was last written |
| `writtenBy` | No | string | Tool/component that wrote the file |

#### JSON Serialization Rules

**Property Naming**:

- Use camelCase for JSON properties (System.Text.Json default)
- C# properties should use PascalCase with `[JsonPropertyName]` attributes if needed
- Maximum property name length: 256 characters

**Value Types**:

- Primitives: `null`, `boolean`, `number`, `string`
- Collections: `array` of any supported type
- Objects: nested JSON objects mapping to POCO properties
- Custom types: must have System.Text.Json converters

**TSettings Interface & POCO Mapping Guidelines**:

```csharp
// TSettings is an interface that defines the settings contract
public interface IEditorSettings
{
    FontSettings Font { get; set; }
    WindowSettings Window { get; set; }
    Secret<string>? ApiKey { get; set; }
}

// POCO implementation used for serialization
public class EditorSettingsPoco : IEditorSettings
{
    [JsonPropertyName("font")]
    public FontSettings Font { get; set; } = new();

    [JsonPropertyName("window")]
    public WindowSettings Window { get; set; } = new();

    [JsonPropertyName("apiKey")]
    public Secret<string>? ApiKey { get; set; }
}

public class FontSettings
{
    [JsonPropertyName("family")]
    public string Family { get; set; } = "Consolas";

    [JsonPropertyName("size")]
    [Range(8, 72)]
    public int Size { get; set; } = 14;

    [JsonPropertyName("weight")]
    public FontWeight Weight { get; set; } = FontWeight.Normal;
}

public class WindowSettings
{
    public SizeSettings Size { get; set; } = new();
    public PointSettings Position { get; set; } = new();

    [JsonPropertyName("isMaximized")]
    public bool IsMaximized { get; set; }
}
```

#### Secrets Handling

**Secret Wrapper Type**:

```csharp
/// <summary>
/// Wrapper for sensitive configuration values that require encryption.
/// </summary>
/// <typeparam name="T">The type of the secret value</typeparam>
public sealed class Secret<T>
{
    private readonly T _value;

    public Secret(T value) => _value = value;

    /// <summary>
    /// Get the secret value. Should only be used when the value is needed.
    /// </summary>
    public T GetValue() => _value;

    /// <summary>
    /// Implicit conversion from T to Secret&lt;T&gt;
    /// </summary>
    public static implicit operator Secret<T>(T value) => new(value);
}
```

**JSON Representation**:

- Secrets are serialized with a `$type` marker and encrypted `value`
- Sources that don't support encryption MUST throw `NotSupportedException` for `Secret<T>` properties
- The secret's inner value is encrypted using the source's encryption mechanism

**Validation Rules**:

- Sources MUST validate they can handle secrets before accepting them in WriteAsync
- Service layer MUST validate `Secret<T>` properties against source capabilities before persistence

## 5. Acceptance Criteria

### Core Functionality

- **AC-001**: **Basic Read/Write Operations**
  - **Given**: A new `ISettingsService<TSettings>` instance that has been initialized
  - **When**: A property is set via the Settings property (e.g., `service.Settings.FontSize = 14`)
  - **Then**: The property value is immediately available and `IsDirty` becomes `true`
  - **And**: After calling `SaveAsync()`, the value persists and `IsDirty` becomes `false`

- **AC-002**: **Multi-Source Last-Wins Strategy**
  - **Given**: Multiple settings sources loaded in sequence
  - **When**: The same property exists in multiple sources with different values
  - **Then**: The value from the last loaded source is used in the final settings
  - **And**: Earlier source values are overridden by later sources

- **AC-003**: **Property Change Notifications**
  - **Given**: A consumer subscribed to `PropertyChanged` events on the settings service
  - **When**: A settings property is modified (via direct assignment or source reload)
  - **Then**: The `PropertyChanged` event fires with the correct property name
  - **And**: The event contains both old and new values when applicable

### Thread Safety & Concurrency

- **AC-004**: **Concurrent Access Safety**
  - **Given**: Multiple threads reading settings properties simultaneously
  - **When**: One thread modifies a property value
  - **Then**: Readers see either the complete old or complete new value (no partial/corrupted state)
  - **And**: All threads eventually see the new value after the write completes

- **AC-005**: **Atomic Persistence**
  - **Given**: A settings file being written to disk
  - **When**: The write operation is interrupted (process kill, power loss, etc.)
  - **Then**: The file is either in the original state or the complete new state
  - **And**: No corrupted/partial content is ever written to disk

### Validation & Error Handling

- **AC-006**: **Property Validation**
  - **Given**: Settings with validation attributes (e.g., `[Range(1, 100)]` on font size)
  - **When**: An invalid value is set (e.g., font size = -5)
  - **Then**: The validation fails before persistence with `SettingsValidationException`
  - **And**: The settings object retains the previous valid value
  - **And**: The validation exception contains detailed error information

- **AC-007**: **Source-Level Error Recovery**
  - **Given**: A settings source that fails to read (e.g., corrupted file, network error)
  - **When**: `InitializeAsync()` or `ReloadAsync()` is called
  - **Then**: The service continues to operate using other available sources
  - **And**: The failure is reported via source change events with `ChangeType.Failed`
  - **And**: The application does not crash or become unusable

- **AC-008**: **Secret Storage Validation**
  - **Given**: A settings source that does not support encryption
  - **When**: Attempting to persist a `Secret<T>` property
  - **Then**: The source validation fails with `NotSupportedException`
  - **And**: No secret data is written to the insecure source
  - **And**: Other non-secret properties can still be persisted

### Source Management

- **AC-009**: **Source Lifecycle Events**
  - **Given**: A consumer subscribed to source change notifications
  - **When**: A settings file is modified externally (e.g., edited by another process)
  - **Then**: A source change event is fired with `ChangeType.Updated`
  - **And**: The settings are automatically reloaded if auto-reload is enabled
  - **And**: Property change notifications fire for any changed values

## Bootstrapper integration and DI registration

The application integrates settings sources during bootstrap using a `WithSettings` extension on the bootstrapper. The `WithSettings` method accepts a collection of file names (and optionally other source descriptors) and registers `ISettingsSource` implementations with the DryIoc container using factory delegates.

Behavior summary:

- `WithSettings` maps file extensions (for example `.json`, `.env`) to concrete `ISettingsSource` implementations such as `JsonSettingsSource`, `EnvSettingsSource`, `EncryptedJsonSettingsSource` and registers them in the container.
- Each settings file is registered as a distinct `ISettingsSource` so the `SettingsManager` (or the `ISettingsService` implementation) can enumerate and load them in order.
- The DryIoc registrations use factory delegates (Made.Of) so configuration (file path) can be captured at registration time.

Example DryIoc registrations created by `WithSettings`:

```csharp
// Register two JSON file-backed settings sources (each bound to a specific file)
container.Register<ISettingsSource>(
  made: Made.Of(() => new JsonSettingsSource("settings1.json")));
container.Register<ISettingsSource>(
  made: Made.Of(() => new JsonSettingsSource("settings2.json")));

// Register an environment-backed settings source (no file path required)
container.Register<ISettingsSource, EnvSettingsSource>();

// Register the settings manager which will enumerate all registered sources
container.Register<SettingsManager>();
```

Settings manager example (consumer of the registered `ISettingsSource` collection):

```csharp
public class SettingsManager
{
  private readonly IEnumerable<ISettingsSource> _sources;

  public SettingsManager(IEnumerable<ISettingsSource> sources)
  {
    _sources = sources;
  }

  public void PrintAll()
  {
    foreach (var src in _sources)
      Console.WriteLine(src.Load());
  }
}

### SettingsManager Contract

The **SettingsManager** is the central orchestrator that manages all settings sources and provides access to settings services. It MUST implement the following contract:

```csharp
/// <summary>
/// Central manager for all settings sources and services.
/// Orchestrates source loading with last-loaded-wins strategy and provides typed settings service instances.
/// </summary>
public interface ISettingsManager : IDisposable
{
    /// <summary>
    /// Get or create a settings service for the specified settings interface.
    /// Each TSettings interface gets its own service instance that implements the interface directly.
    /// </summary>
    /// <typeparam name="TSettings">The settings interface type</typeparam>
    /// <returns>Settings service that implements the specified interface</returns>
    ISettingsService<TSettings> GetService<TSettings>() where TSettings : class;

    /// <summary>
    /// Initialize all registered sources and load initial settings data.
    /// Sources are loaded in registration order with last-loaded-wins strategy.
    /// Must be called before accessing any settings services.
    /// </summary>
    /// <param name="ct">Cancellation token</param>
    /// <returns>Task that completes when initialization is finished</returns>
    Task InitializeAsync(CancellationToken ct = default);

    /// <summary>
    /// Reload all sources and update all settings services.
    /// Applies last-loaded-wins strategy and triggers change notifications for any modified values.
    /// </summary>
    /// <param name="ct">Cancellation token</param>
    /// <returns>Task that completes when reload is finished</returns>
    Task ReloadAllAsync(CancellationToken ct = default);

    /// <summary>
    /// Save all modified settings across all services to their appropriate sources.
    /// Each source handles its own serialization and persistence.
    /// </summary>
    /// <param name="ct">Cancellation token</param>
    /// <returns>Task that completes when all saves are finished</returns>
    Task SaveAllAsync(CancellationToken ct = default);

    /// <summary>
    /// Run migrations across all sources for schema evolution.
    /// </summary>
    /// <param name="ct">Cancellation token</param>
    /// <returns>Task that completes when migrations are finished</returns>
    Task RunMigrationsAsync(CancellationToken ct = default);

    /// <summary>
    /// Subscribe to source-level lifecycle events across all sources.
    /// </summary>
    /// <param name="handler">Event handler for source changes</param>
    /// <returns>Disposable subscription</returns>
    IDisposable SubscribeToSourceChanges(Action<SettingsSourceChangedEventArgs> handler);
}

/// <summary>
/// Concrete implementation of the settings manager.
/// </summary>
public class SettingsManager : ISettingsManager
{
    private readonly IEnumerable<ISettingsSource> _sources;
    private readonly Dictionary<Type, object> _services = new();
    private readonly ILogger<SettingsManager>? _logger;

    /// <summary>
    /// Constructor receives the full collection of registered sources.
    /// Sources will be loaded in registration order (last loaded wins).
    /// </summary>
    /// <param name="sources">All registered settings sources</param>
    /// <param name="logger">Optional logger for diagnostics</param>
    public SettingsManager(IEnumerable<ISettingsSource> sources, ILogger<SettingsManager>? logger = null)
    {
        _sources = sources;
        _logger = logger;
    }

    // Implementation details...
}
```

### DI Registration Strategy

The bootstrapper registers sources and the central SettingsManager, which then provides settings service instances on demand:

```csharp
// Register individual sources with their configuration
container.Register<ISettingsSource>(made: Made.Of(() => new JsonSettingsSource("editor.json")));
container.Register<ISettingsSource>(made: Made.Of(() => new JsonSettingsSource("editor.local.json")));
container.Register<ISettingsSource>(made: Made.Of(() => new EncryptedJsonSettingsSource("secrets.json")));

// Register the central settings manager as singleton
container.Register<ISettingsManager, SettingsManager>(Reuse.Singleton);

// Settings services are created on-demand by the manager, not directly registered
// Consumers request ISettingsService<TSettings> and get it from the manager
container.Register<ISettingsService<TSettings>>(
    made: Made.Of(r => ServiceInfo.Of<ISettingsManager>(),
                  manager => manager.GetService<TSettings>()));
```

Notes:

- The `ISettingsSource` abstraction is intentionally focused on text I/O and lifecycle. The service-layer (`SettingsManager` / `ISettingsService`) is responsible for parsing, validation, merging, and property-level semantics.
- Using DryIoc factories preserves registration-time information (file path, options) while still allowing the DI container to resolve the full set of sources as an `IEnumerable<ISettingsSource>`.

## 6. Test Automation Strategy

### Test Architecture

#### Test Levels & Scope

| Test Level | Scope | Tools | Coverage Target |
|------------|-------|-------|-----------------|
| **Unit Tests** | Individual components, service logic, validation | xUnit, Moq, FluentAssertions | ≥90% for core logic |
| **Integration Tests** | Settings sources, file I/O, serialization | xUnit, Temporary files | ≥80% for I/O operations |
| **Contract Tests** | Interface compliance, DI registration | xUnit, Test containers | 100% for public contracts |
| **Performance Tests** | Latency, throughput, memory usage | BenchmarkDotNet | Baseline establishment |
| **Security Tests** | Secret encryption, access control | Custom security harness | 100% for security features |

#### Test Organization Structure

```text
Tests/
├── DroidNet.Settings.Tests/              # Unit tests
│   ├── Services/
│   │   ├── SettingsServiceTests.cs       # Core service functionality
│   │   ├── ValidationTests.cs            # Property validation logic
│   │   └── NotificationTests.cs          # Change notification behavior
│   ├── Sources/
│   │   ├── JsonSettingsSourceTests.cs    # JSON source implementation
│   │   └── EncryptedSourceTests.cs       # Encrypted source implementation
│   └── Serialization/
│       ├── JsonSerializationTests.cs     # JSON serialization/deserialization
│       └── SecretSerializationTests.cs   # Secret wrapper handling
├── DroidNet.Settings.Integration.Tests/  # Integration tests
│   ├── MultiSourceTests.cs               # Multiple sources with last-loaded-wins
│   ├── ConcurrencyTests.cs               # Thread safety and concurrent access
│   ├── FileSystemTests.cs                # Real file system operations

└── DroidNet.Settings.Security.Tests/     # Security-focused tests
    ├── SecretEncryptionTests.cs           # Secret encryption verification
    └── AccessControlTests.cs              # Security boundary testing
```

### Test Implementation Guidelines

#### Unit Test Patterns

```csharp
[TestClass]
public class SettingsServiceTests
{
    private Mock<ISettingsSource> _mockSource;
    private SettingsService<ITestSettings> _service;

    [TestInitialize]
    public void Setup()
    {
        _mockSource = new Mock<ISettingsSource>();

        // Configure mock source behavior
        _mockSource.Setup(s => s.CanWrite).Returns(true);
        _mockSource.Setup(s => s.SupportsSecrets).Returns(false);

        _service = new SettingsService<ITestSettings>(new[] { _mockSource.Object });
    }

    [TestMethod]
    public async Task SaveAsync_WithValidSettings_ShouldPersistSuccessfully()
    {
        // Arrange
        _service.Settings.FontSize = 14;  // Access via Settings property
        var expectedMetadata = new SettingsMetadata { Version = "1.0", SchemaVersion = "2025-10-18" };

        _mockSource.Setup(s => s.SaveAsync(It.IsAny<object>(), It.IsAny<SettingsMetadata>(), It.IsAny<CancellationToken>()))
                  .ReturnsAsync(SettingsSourceWriteResult.CreateSuccess("v1.0"));

        // Act
        await _service.SaveAsync();

        // Assert
        _mockSource.Verify(s => s.SaveAsync(
            It.Is<object>(data => HasProperty(data, "FontSize", 14)),
            It.IsAny<SettingsMetadata>(),
            It.IsAny<CancellationToken>()), Times.Once);

        _service.IsDirty.Should().BeFalse();
    }

    private static bool HasProperty(object obj, string propertyName, object expectedValue)
    {
        var property = obj.GetType().GetProperty(propertyName);
        return property?.GetValue(obj)?.Equals(expectedValue) == true;
    }
}
```

#### Integration Test Patterns

```csharp
[TestClass]
public class MultiSourceIntegrationTests
{
    private TemporaryDirectory _tempDir;
    private string _defaultsFile;
    private string _userFile;
    private string _localFile;

    [TestInitialize]
    public void Setup()
    {
        _tempDir = new TemporaryDirectory();
        _defaultsFile = Path.Combine(_tempDir.Path, "defaults.json");
        _userFile = Path.Combine(_tempDir.Path, "user.json");
        _localFile = Path.Combine(_tempDir.Path, "local.json");

        // Create test files with known content
        File.WriteAllText(_defaultsFile, """
            {
                "metadata": { "version": "1.0", "schemaVersion": "2025-10-18" },
                "settings": { "fontSize": 12, "theme": "light" }
            }
            """);
    }

    [TestMethod]
    public async Task MultipleSourcesLastWins_ShouldMergeCorrectly()
    {
        // Arrange
        var sources = new ISettingsSource[]
        {
            new JsonSettingsSource(_defaultsFile),   // Loaded first
            new JsonSettingsSource(_userFile),      // Loaded second, overrides defaults
            new JsonSettingsSource(_localFile)      // Loaded last, wins over all
        };

        var service = new SettingsService<IAppSettings>(sources);

        // Act
        await service.InitializeAsync();

        // Assert
        service.Settings.FontSize.Should().Be(16); // From local file (last loaded)
        // Additional assertions for last-wins testing...
    }

    [TestCleanup]
    public void Cleanup()
    {
        _tempDir?.Dispose();
    }
}
```

### Test Data Management

#### Test Fixtures & Data

- **Deterministic Test Data**: Use fixed JSON files with known values for predictable test outcomes
- **Temporary Isolation**: Each test gets isolated temporary directories to prevent cross-test contamination

- **Invalid Data Sets**: Deliberately corrupted/invalid files to test error handling

#### Mock Objects & Stubs

- **Settings Source Mocks**: Simulate various source behaviors (success, failure, slow response)
- **Encryption Mocks**: Test secret handling without real encryption dependencies
- **File System Mocks**: Test file system edge cases without actual file operations

### Security Testing

#### Secret Encryption Verification

- **Encryption Roundtrip**: Verify secrets can be encrypted, stored, and decrypted correctly
- **Key Management**: Test key rotation and secure key storage scenarios
- **Access Control**: Verify unauthorized access attempts fail appropriately
- **Side-Channel Protection**: Ensure secrets don't leak through logs, exceptions, or serialization

## 7. Rationale & Context

- Single service interface reduces scattered ad-hoc config access and makes it possible to add validation centrally.

- Settings source pattern isolates filesystem-level storage details and allows adding encryption or cloud-backed file
  synchronization later without changing consumers.

- Explicit versioning ensures forward compatibility and safe upgrades.

## 8. Dependencies & External Integrations

### Platform Dependencies

- **PLT-001**: **.NET Runtime**: .NET 8+ as specified in repository global.json
- **PLT-002**: **Dependency Injection**: DryIoc container for service registration and resolution
- **PLT-003**: **JSON Serialization**: System.Text.Json (no Newtonsoft.Json dependency)
- **PLT-004**: **File System**: Atomic file operations support (rename/replace) on Windows, Linux, macOS

### Infrastructure Dependencies

- **INF-001**: **File System Access**: Read/write permissions to application settings directory
- **INF-002**: **Temporary Directory**: Access to system temp directory for atomic write operations
- **INF-003**: **File Watching**: File system change notification support (optional, for external change detection)

### Optional External Integrations

- **EXT-001**: **Cloud Storage**: Optional cloud-synchronized settings source implementation
- **EXT-002**: **Key Management**: Optional key vault service for secret encryption keys
- **EXT-003**: **Configuration Services**: Optional integration with external configuration services (Azure App Configuration, etc.)

### Security Dependencies

- **SEC-001**: **Encryption Libraries**: Built-in .NET cryptographic libraries for secret encryption
- **SEC-002**: **Key Storage**: Platform-specific secure key storage (Windows DPAPI, Linux keyring, macOS Keychain)
- **SEC-003**: **Audit Logging**: Optional integration with organizational audit logging systems

### Development Dependencies

- **DEV-001**: **Testing Frameworks**: xUnit or MSTest for unit and integration tests
- **DEV-002**: **Mocking Libraries**: Moq for test doubles and mocks
- **DEV-003**: **Assertion Libraries**: FluentAssertions for readable test assertions
- **DEV-004**: **Benchmarking**: BenchmarkDotNet for performance testing (optional)

### Compliance Requirements

- **COM-001**: **Data Protection**: Must comply with organizational data protection policies for secret storage
- **COM-002**: **Audit Trail**: May require audit logging for secret access and modification
- **COM-003**: **Encryption Standards**: Must use approved encryption algorithms and key sizes

## 9. Usage Examples & Edge Cases

### Basic Usage Scenarios

#### Simple Settings Access

```csharp
// Define your settings interface
public interface IEditorSettings
{
    [Range(8, 72)]
    int FontSize { get; set; }

    string FontFamily { get; set; }

    WindowSettings Window { get; set; }

    Secret<string>? ApiKey { get; set; }
}

public class WindowSettings
{
    public int Width { get; set; } = 1024;
    public int Height { get; set; } = 768;
    public bool IsMaximized { get; set; } = false;
}

// Bootstrap and register settings sources (no precedence, last loaded wins)
services.AddSettings<IEditorSettings>(builder =>
{
    builder.AddJsonFile("defaults.json")
           .AddJsonFile("settings.json")
           .AddJsonFile("settings.local.json", optional: true)
           .AddEncryptedJsonFile("secrets.json", optional: true);
});

// Use in application code
public class EditorService
{
    private readonly ISettingsService<IEditorSettings> _settings;

    public EditorService(ISettingsService<IEditorSettings> settings)
    {
        _settings = settings;

        // Subscribe to property changes
        _settings.PropertyChanged += OnSettingsChanged;

        // Subscribe to source lifecycle events
        _settings.SubscribeToSourceChanges(OnSourceChanged);
    }

    private void OnSettingsChanged(object? sender, PropertyChangedEventArgs e)
    {
        Console.WriteLine($"Setting {e.PropertyName} changed");

        // React to specific property changes
        if (e.PropertyName == nameof(IEditorSettings.FontSize))
        {
            UpdateFontSize(_settings.Settings.FontSize);  // Access via Settings property
        }
    }

    private void OnSourceChanged(SettingsSourceChangedEventArgs e)
    {
        Console.WriteLine($"Source {e.SourceId} {e.ChangeType}: {e.Details}");
    }

    public async Task UpdateFontAsync(int newSize)
    {
        // Modify settings via Settings property
        _settings.Settings.FontSize = newSize;

        // Validate before saving
        var validationErrors = await _settings.ValidateAsync();
        if (validationErrors.Any())
        {
            throw new InvalidOperationException($"Invalid font size: {string.Join(", ", validationErrors.Select(e => e.Message))}");
        }

        // Persist changes
        await _settings.SaveAsync();
    }
}
```

#### Advanced Multi-Source Configuration

```csharp
// Configure multiple sources (last loaded wins, no precedence)
services.AddSettings<IAppSettings>(builder =>
{
    // Load in order - each subsequent source overrides previous ones
    builder.AddEmbeddedJson("defaults.json")  // Loaded first

           // User-specific settings override defaults
           .AddJsonFile(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "MyApp", "settings.json"))

           // Machine-specific overrides
           .AddJsonFile("machine.settings.json", optional: true)

           // Environment-specific secrets
           .AddEncryptedJsonFile("secrets.json", optional: true)

           // Development overrides (loaded last, wins over all previous)
           .AddJsonFile("settings.development.json", optional: true, developmentOnly: true);
});
```

### Error Handling Scenarios

#### Validation Failures

```csharp
try
{
    _settings.Settings.FontSize = -1; // Invalid value, access via Settings property
    await _settings.SaveAsync();
}
catch (SettingsValidationException ex)
{
    foreach (var error in ex.ValidationErrors)
    {
        Console.WriteLine($"Validation error in {error.PropertyPath}: {error.Message}");
        // Output: "Validation error in FontSize: Value must be between 8 and 72"
    }
}
```

#### Source Failures

```csharp
try
{
    await _settings.InitializeAsync();
}
catch (SettingsInitializationException ex)
{
    // Some sources failed to load, but service is still usable with available sources
    Console.WriteLine($"Settings initialization completed with errors: {ex.Message}");

    // Check which sources are available
    var availableSources = ex.SuccessfulSources;
    var failedSources = ex.FailedSources;
}
```

#### Concurrent Modification

```csharp
try
{
    await _settings.SaveAsync();
}
catch (SettingsPersistenceException ex) when (ex.Message.Contains("concurrent modification"))
{
    // Handle optimistic concurrency conflict
    await _settings.ReloadAsync(); // Reload from sources

    // Reapply user changes or prompt for resolution
    await ResolveConcurrencyConflictAsync();
}
```

### Edge Cases & Limitations

#### Missing or Corrupted Files

- **Behavior**: Service initializes successfully using available sources
- **Notification**: Failed sources trigger `SourceChanged` events with `ChangeType.Failed`
- **Recovery**: Sources can be retried via `ReloadAsync()` after external repair

#### Type Deserialization Failures

```csharp
// If JSON contains invalid data for the target type
try
{
    await _settings.ReloadAsync();
}
catch (SettingsDeserializationException ex)
{
    Console.WriteLine($"Failed to deserialize {ex.PropertyPath}: {ex.Message}");
    // Property retains previous valid value or falls back to default
}
```

#### Secret Storage Limitations

```csharp
// Attempting to save secrets to a non-encrypted source
try
{
    _settings.Settings.ApiKey = new Secret<string>("my-secret");  // Access via Settings property
    await _settings.SaveAsync();
}
catch (NotSupportedException ex)
{
    Console.WriteLine($"Cannot save secrets to unencrypted source: {ex.Message}");
    // Configure an encrypted source or remove the secret property
}
```

#### Performance Considerations

- **Property Access**: Direct property access on settings service is always fast (in-memory)
- **File Watching**: Large numbers of file watchers may impact performance on some platforms
- **Large Settings Files**: Files >1MB may impact initialization time
- **Frequent Saves**: Rapid consecutive `SaveAsync()` calls should be debounced by consumers

#### Thread Safety Guarantees

- **Reads**: Multiple threads can safely read properties simultaneously
- **Writes**: Property assignments are thread-safe at the individual property level
- **Persistence**: Only one `SaveAsync()` operation per service instance can execute at a time
- **Reloading**: `ReloadAsync()` coordinates with ongoing operations to prevent corruption

## 10. Validation Criteria & Quality Gates

### Implementation Validation

#### API Contract Compliance

- **Static Compilation**: All public interfaces and contracts from Section 4 compile without errors
- **Type Safety**: Generic type constraints and nullable reference types are correctly applied
- **Documentation**: All public APIs have complete XML documentation comments
- **Backwards Compatibility**: No breaking changes to existing contracts (when applicable)

#### Functional Verification

| Validation Area | Acceptance Criteria | Verification Method |
|----------------|-------------------|-------------------|
| **Core Functionality** | All AC-001 through AC-012 pass | Automated unit and integration tests |
| **Thread Safety** | No race conditions under concurrent load | Stress testing with multiple threads |
| **Atomic Operations** | File corruption impossible during writes | File system failure injection tests |
| **Error Handling** | Graceful degradation, no unhandled exceptions | Fault injection and error scenario tests |
| **Performance** | Meets latency and throughput requirements | Automated benchmarking with baselines |

#### Security Validation

- **Secret Protection**: Encrypted secrets never appear as plaintext in storage or logs
- **Access Control**: Unauthorized access attempts fail with appropriate errors
- **Key Management**: Encryption keys are properly protected and rotated
- **Audit Trail**: Security-relevant operations are logged (when configured)

## 11. Related Documentation & References

### Internal Documentation

- **Design Rationale**: `projects/Config/docs/settings-design.md` - Design and architectural decisions

- **Security Guidelines**: `projects/Config/docs/settings-security.md` - Security considerations and threat model

### External Standards & References

- **JSON Schema**: [JSON Schema Specification](https://json-schema.org/) for settings file validation
- **System.Text.Json**: [Microsoft Documentation](https://docs.microsoft.com/en-us/dotnet/standard/serialization/system-text-json-overview)
- **Data Protection**: [.NET Data Protection APIs](https://docs.microsoft.com/en-us/aspnet/core/security/data-protection/)

### Compliance & Security References

- **OWASP Guidelines**: [Configuration and Deployment Management](https://owasp.org/www-project-top-ten/)
- **NIST Guidelines**: [Application Security Configuration Management](https://csrc.nist.gov/publications/)
- **Organizational Security Policies**: Internal security guidelines for encryption and key management

### Technology References

- **DryIoc Documentation**: [Container registration patterns and best practices](https://github.com/dadhi/DryIoc)
- **MSTest Documentation**: [Testing patterns and conventions](https://learn.microsoft.com/en-us/dotnet/core/testing/unit-testing-mstest-intro)
- **Testably**: [Testably](https://github.com/Testably/Testably.Abstractions)
- **BenchmarkDotNet**: [Performance testing and benchmarking](https://benchmarkdotnet.org/articles/overview.html)
