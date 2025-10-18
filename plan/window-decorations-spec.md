# Introduction

This specification defines the design and implementation requirements for the Aura Window Decoration System (Phase 4), which provides a strongly-typed, immutable, and persistable window decoration framework for Aura-managed windows. The system enables customization of window chrome, title bars, menus, buttons, and backdrop effects while maintaining type safety, graceful degradation, and seamless integration with existing Aura modules.

## 1. Purpose & Scope

### Purpose

The Window Decoration System provides application developers with:

1. A type-safe, immutable configuration system for window chrome customization
2. Preset-based defaults covering 90% of common use cases
3. Fluent builder APIs for intuitive custom decoration creation
4. Type-safe integration with the Menus module via the IMenuProvider abstraction
5. Persistence of user decoration preferences via the Config module
6. Theme coordination with the AppearanceSettingsService
7. Backward compatibility with existing IWindowManagerService API

### Scope

**In Scope:**

- WindowDecorationOptions record structure and nested option types
- Built-in presets (Main, Document, Tool, Secondary, System, Transient, Modal)
- Fluent builder API for custom decorations
- IMenuProvider abstraction for menu integration
- Persistence DTOs and ISettingsService integration
- WindowBackdropService for theme coordination
- WindowContext integration
- MainShellView XAML binding patterns
- Validation and error handling
- Thread safety for concurrent window creation

**Out of Scope:**

- Per-window-ID decoration overrides in settings (deferred)
- Custom title bar layout templates (deferred to Phase 5)
- Runtime decoration mutation (decorations are immutable per window lifetime)
- Plugin-based menu provider loading (only DI-registered providers supported)

### Intended Audience

- .NET/C# developers implementing Aura-based applications
- Generative AI systems generating Aura window management code
- QA engineers writing automated tests for window decoration features
- Technical documentation authors

### Assumptions

- The Aura module's WindowManagerService and WindowContext are already implemented
- The Menus module provides IMenuSource with ObservableCollection-based menu items
- The Config module provides `ISettingsService<T>` for JSON-based settings persistence
- IAppearanceSettings manages application theme modes and will be extended to include application-wide backdrop
- WinUI 3 is the target UI framework with MicaBackdrop, DesktopAcrylicBackdrop APIs available
- .NET 8.0 or later is the target runtime

## 2. Definitions

| Term | Definition |
|------|------------|
| **Window Decoration** | The visual and functional elements of a window's chrome, including title bar, buttons, menu, and backdrop |
| **Chrome** | The window frame and title bar provided by Aura (as opposed to system chrome) |
| **Backdrop** | The visual material effect applied to the window background (Mica, MicaAlt, Acrylic, or None) |
| **Preset** | A predefined WindowDecorationOptions configuration for common window categories |
| **Menu Provider** | A factory service (IMenuProvider) that creates IMenuSource instances per window |
| **Drag Region** | The area of the title bar that allows window dragging |
| **System Title Bar Overlay** | WinUI feature allowing content to extend into the title bar region |
| **Window Category** | A readonly record struct with static predefined constants (Main, Secondary, Document, Tool, Transient, Modal, System) and case-insensitive string value equality |
| **Immutable Options** | WindowDecorationOptions instances that cannot be modified after creation |
| **WindowContext** | A class with required init properties and mutable activation state (IsActive, LastActivatedAt). Not fully immutable - activation state is mutated in-place |
| **Fluent Builder** | A builder pattern API using method chaining for readability |

## 3. Requirements, Constraints & Guidelines

### Functional Requirements

- **REQ-001**: The system SHALL provide a WindowDecorationOptions immutable record with properties for Category, ChromeEnabled, TitleBar, Buttons, Menu, Backdrop, and EnableSystemTitleBarOverlay
- **REQ-002**: The system SHALL provide built-in presets: Main, Document, Tool, Secondary, and System, Transient, Modal
- **REQ-003**: Each preset SHALL have sensible defaults suitable for 90% of use cases in that category
- **REQ-004**: The system SHALL provide a fluent WindowDecorationBuilder API for creating custom decorations
- **REQ-005**: The builder API SHALL support starting from a preset and customizing specific properties
- **REQ-006**: The system SHALL integrate with the Menus module via an IMenuProvider abstraction
- **REQ-007**: IMenuProvider SHALL create new IMenuSource instances per window to avoid shared mutable state
- **REQ-008**: The system SHALL persist decoration preferences via WindowDecorationSettings using the Config module's `SettingsService<T>` infrastructure with `IOptionsMonitor<WindowDecorationSettings>` change tracking
- **REQ-009**: The system SHALL use System.Text.Json for serialization with custom converters for non-serializable properties
- **REQ-010**: Menu providers SHALL NOT be persisted; only provider IDs SHALL be stored via MenuOptionsJsonConverter
- **REQ-011**: The system SHALL validate decoration options and throw clear ArgumentException for invalid combinations
- **REQ-012**: WindowContext SHALL include a nullable Decoration property
- **REQ-013**: WindowManagerService SHALL resolve decorations via explicit parameter, registry lookup, or type inference
- **REQ-014**: The system SHALL provide WindowBackdropService that subscribes to window lifecycle events and automatically applies backdrops when windows are created based on WindowDecorationOptions.Backdrop
- **REQ-015**: The system SHALL support graceful degradation when menu providers are not found
- **REQ-016**: The system SHALL allow opting out of Aura chrome via ChromeEnabled=false
- **REQ-017**: WindowDecorationOptions SHALL be immutable after creation (record with init-only properties)
- **REQ-018**: The system SHALL provide extension methods for DI registration of decoration services and menu providers
- **REQ-019**: Menu provider creation SHALL be thread-safe for concurrent window activation
- **REQ-020**: The system SHALL log warnings for missing menu providers without throwing exceptions
- **REQ-021**: WindowCategory SHALL be implemented as a readonly record struct with static predefined constants and case-insensitive string value equality
- **REQ-022**: MainShellView SHALL bind to WindowContext.Decoration properties for data-driven chrome rendering
- **REQ-023**: The system SHALL support MenuBar for persistent menus (IsCompact=false) and ExpandableMenuBar for compact menus (IsCompact=true)
- **REQ-024**: Window control buttons SHALL use WinUI 3 native caption buttons via ExtendsContentIntoTitleBar=true
- **REQ-025**: Title bar height SHALL be controlled by binding to Decoration.TitleBar.Height, not dynamically calculated
- **REQ-026**: Icon visibility SHALL be controlled by binding to Decoration.TitleBar.ShowIcon
- **REQ-027**: The system SHALL preserve existing SettingsMenu (Settings/Themes) independent of WindowContext.MenuSource

### Security Requirements

- **SEC-001**: Menu providers SHALL be resolved only from the DI container, not from external plugin files
- **SEC-002**: Settings SHALL be loaded only from the trusted application data folder
- **SEC-003**: Type resolution SHALL NOT use reflection on user-provided string type names
- **SEC-004**: Validation SHALL prevent malicious option combinations (e.g., negative padding, zero height)
- **SEC-005**: Menu provider IDs SHALL be validated as non-empty strings

### Performance Requirements

- **PER-001**: Decoration resolution and validation SHALL complete in ≤ 2ms per window creation
- **PER-002**: Menu provider resolution SHALL be O(n) where n is the number of registered providers (typically < 10)
- **PER-003**: Menu source creation SHALL occur once per window, not per activation
- **PER-004**: Settings deserialization SHALL be lazy and on-demand, not in window creation hot path

### Constraints

- **CON-001**: WindowDecorationOptions and nested option types MUST be immutable records
- **CON-002**: No breaking changes to existing IWindowManagerService API are permitted
- **CON-003**: Menu providers MUST implement IMenuProvider with a string ProviderId and CreateMenuSource method
- **CON-004**: Decoration options MUST NOT store mutable IMenuSource references (only provider IDs)
- **CON-005**: The system MUST NOT introduce dependencies on third-party serialization libraries
- **CON-006**: All public APIs MUST have XML documentation comments
- **CON-007**: Decorations are bound once per window lifetime; runtime mutation is not supported
- **CON-008**: The system MUST integrate with existing Config module's `ISettingsService<T>` pattern
- **CON-009**: Backdrop is a non-nullable BackdropKind enum with default value BackdropKind.None. WindowBackdropService applies backdrops based on WindowDecorationOptions.Backdrop value, skipping application when value is None or null decoration
- **CON-010**: Main window MUST have a Close button to ensure proper application shutdown

### Guidelines

- **GUD-001**: Use presets for simple cases; use builders for advanced customization
- **GUD-002**: Register menu providers in DI startup using ServiceCollectionExtensions
- **GUD-003**: Prefer explicit decoration parameters over registry-based resolution for clarity
- **GUD-004**: Use with expressions for small customizations to presets
- **GUD-005**: Validate custom decorations early by calling Build() on builders
- **GUD-006**: Log decoration resolution at Information level for debugging window creation issues
- **GUD-007**: Use null for MenuOptions to indicate no menu should be displayed
- **GUD-008**: Use BackdropKind.None to explicitly disable backdrop effects
- **GUD-010**: Resolve menu providers using `IEnumerable<IMenuProvider>` from DI, not service locator pattern

### Patterns

- **PAT-001**: Use immutable records with init-only properties for all option types
- **PAT-002**: Use factory methods (presets) for common configurations
- **PAT-003**: Use fluent builders for complex configurations
- **PAT-004**: Use JsonConverter attributes for custom serialization of complex types
- **PAT-005**: Use provider pattern (IMenuProvider) for creating window-scoped dependencies
- **PAT-006**: Use validation methods that throw on invalid state rather than returning bool
- **PAT-007**: Use required properties for mandatory configuration (Category)
- **PAT-008**: Use nullable types for optional configuration (Menu, IconSource)
- **PAT-009**: Use extension methods for DI service registration
- **PAT-010**: Use coordinator services (WindowBackdropService) for cross-cutting concerns

## 4. Interfaces & Data Contracts

### WindowDecorationOptions

```csharp
/// <summary>
/// Immutable configuration for window decoration (chrome, menu, buttons, backdrop).
/// </summary>
public sealed record WindowDecorationOptions
{
    /// <summary>Window category for semantic grouping.</summary>
    public required WindowCategory Category { get; init; }

    /// <summary>Enable Aura custom chrome. If false, system title bar is used.</summary>
    public bool ChromeEnabled { get; init; } = true;

    /// <summary>Title bar configuration.</summary>
    public TitleBarOptions TitleBar { get; init; } = TitleBarOptions.Default;

    /// <summary>Window button configuration.</summary>
    public WindowButtonsOptions Buttons { get; init; } = WindowButtonsOptions.Default;

    /// <summary>Menu configuration. Null means no menu.</summary>
    public MenuOptions? Menu { get; init; }

    /// <summary>
    /// Backdrop effect for this window. Default is BackdropKind.None.
    /// Set to explicit value to apply backdrop effect.
    /// </summary>
    public BackdropKind Backdrop { get; init; } = BackdropKind.None;

    /// <summary>Enable extending content into title bar (system overlay).</summary>
    public bool EnableSystemTitleBarOverlay { get; init; } = false;

    /// <summary>
    /// Validates option consistency. Throws ArgumentException if invalid.
    /// </summary>
    public void Validate();
}
```

### TitleBarOptions

```csharp
/// <summary>Title bar decoration options.</summary>
public sealed record TitleBarOptions
{
    public static readonly TitleBarOptions Default = new();

    /// <summary>Title bar height in pixels. Null uses system default.</summary>
    public double? Height { get; init; }

    /// <summary>Left padding in pixels.</summary>
    public double PaddingLeft { get; init; } = 0;

    /// <summary>Right padding in pixels.</summary>
    public double PaddingRight { get; init; } = 0;

    /// <summary>Icon URI. Null uses application icon.</summary>
    public Uri? IconSource { get; init; }

    /// <summary>Drag region behavior.</summary>
    public DragRegionBehavior DragBehavior { get; init; } = DragRegionBehavior.Default;
}
```

### WindowButtonsOptions

```csharp
/// <summary>Window button visibility and placement.</summary>
public sealed record WindowButtonsOptions
{
    public static readonly WindowButtonsOptions Default = new();

    public bool ShowMinimize { get; init; } = true;
    public bool ShowMaximize { get; init; } = true;
    public bool ShowClose { get; init; } = true;
    public ButtonPlacement Placement { get; init; } = ButtonPlacement.Right;
}
```

### MenuOptions

```csharp
/// <summary>Menu decoration options.</summary>
[JsonConverter(typeof(MenuOptionsJsonConverter))]
public sealed record MenuOptions
{
    /// <summary>Menu provider identifier (resolved via DI).</summary>
    public required string MenuProviderId { get; init; }

    /// <summary>Use compact rendering for tool windows.</summary>
    public bool IsCompact { get; init; } = false;
}
```

### Enumerations

```csharp
/// <summary>Backdrop material kinds.</summary>
public enum BackdropKind
{
    None,      // No backdrop effect
    Mica,      // Base Mica backdrop
    MicaAlt,   // Alternative Mica backdrop
    Acrylic    // Desktop acrylic backdrop
}

/// <summary>Title bar drag region behaviors.</summary>
public enum DragRegionBehavior
{
    Default,   // Standard drag region in title bar
    Expand,    // Extended drag region
    None       // No drag region (for custom interaction)
}

/// <summary>Window button placement.</summary>
public enum ButtonPlacement
{
    Left,      // Buttons on left side (macOS style)
    Right,     // Buttons on right side (Windows style)
    Auto       // Platform-dependent placement
}
```

### IMenuProvider

```csharp
/// <summary>
/// Provides menu instances for windows. Registered in DI container.
/// </summary>
public interface IMenuProvider
{
    /// <summary>Unique identifier for this menu provider.</summary>
    string ProviderId { get; }

    /// <summary>
    /// Creates a menu source for a window instance.
    /// Must be thread-safe for concurrent window creation.
    /// </summary>
    IMenuSource CreateMenuSource();
}
```

### MenuProvider

```csharp
/// <summary>
/// Thread-safe menu provider using MenuBuilder factory function.
/// </summary>
public sealed class MenuProvider : IMenuProvider
{
    public MenuProvider(string providerId, Func<MenuBuilder> builderFactory);

    public string ProviderId { get; }

    public IMenuSource CreateMenuSource();
}
```

### ScopedMenuProvider

```csharp
/// <summary>
/// Menu provider that resolves dependencies from DI scope per menu creation.
/// </summary>
public sealed class ScopedMenuProvider : IMenuProvider
{
    public ScopedMenuProvider(
        string providerId,
        Action<MenuBuilder, IServiceProvider> configure,
        IServiceProvider serviceProvider);

    public string ProviderId { get; }

    public IMenuSource CreateMenuSource();
}
```

### WindowDecorationBuilder

```csharp
/// <summary>
/// Fluent builder for WindowDecorationOptions with preset factory methods.
/// </summary>
public sealed class WindowDecorationBuilder
{
    /// <summary>Preset menu provider ID constants.</summary>
    public const string PrimaryMenuProvider = "Aura.Menu.Primary";
    public const string DocumentMenuProvider = "Aura.Menu.Document";
    public const string ToolMenuProvider = "Aura.Menu.Tool";

    // Preset factory methods

    /// <summary>
    /// Creates builder for Main window: full chrome, menu, all buttons, MicaAlt.
    /// </summary>
    public static WindowDecorationBuilder ForMainWindow(string? menuProviderId = PrimaryMenuProvider);

    /// <summary>
    /// Creates builder for Document window: full chrome, optional menu, Mica backdrop.
    /// </summary>
    public static WindowDecorationBuilder ForDocumentWindow(string? menuProviderId = DocumentMenuProvider);

    /// <summary>
    /// Creates builder for Tool window: compact menu, no maximize, minimal chrome.
    /// </summary>
    public static WindowDecorationBuilder ForToolWindow(string? menuProviderId = ToolMenuProvider);

    /// <summary>
    /// Creates builder for Secondary/dialog window: minimal chrome, no menu.
    /// </summary>
    public static WindowDecorationBuilder ForSecondaryWindow();

    /// <summary>
    /// Creates builder with system chrome only: Aura decoration disabled.
    /// </summary>
    public static WindowDecorationBuilder WithSystemChromeOnly();

    /// <summary>
    /// Creates builder with default options for custom configuration.
    /// </summary>
    public static WindowDecorationBuilder CreateNew();

    // Fluent customization methods

    public WindowDecorationBuilder WithCategory(string category);
    public WindowDecorationBuilder WithChrome(bool enabled = true);
    public WindowDecorationBuilder WithMenu(string menuProviderId, bool isCompact = false);
    public WindowDecorationBuilder WithoutMenu();
    public WindowDecorationBuilder WithBackdrop(BackdropKind backdrop);
    public WindowDecorationBuilder WithTitleBar(Action<TitleBarBuilder> configure);
    public WindowDecorationBuilder WithButtons(Action<ButtonsBuilder> configure);
    public WindowDecorationBuilder WithSystemTitleBarOverlay(bool enabled = true);

    /// <summary>Builds and validates the options. Throws if invalid.</summary>
    public WindowDecorationOptions Build();
}
```

### WindowDecorationSettings

```csharp
/// <summary>
/// Persistent settings for window decoration preferences.
/// Dictionaries use string comparers that match lookup semantics.
/// </summary>
public sealed class WindowDecorationSettings
{
    /// <summary>Default decoration by semantic category (case-insensitive).</summary>
    public IDictionary<string, WindowDecorationOptions> DefaultsByCategory { get; }
        = new Dictionary<string, WindowDecorationOptions>(StringComparer.OrdinalIgnoreCase);

    /// <summary>Per-window-type overrides (case-sensitive).</summary>
    public IDictionary<string, WindowDecorationOptions> OverridesByType { get; }
        = new Dictionary<string, WindowDecorationOptions>(StringComparer.Ordinal);
}

/// <summary>
/// Domain contract exposed by the decoration settings service.
/// </summary>
public interface IWindowDecorationSettings
{
    IReadOnlyDictionary<string, WindowDecorationOptions> DefaultsByCategory { get; }
    IReadOnlyDictionary<string, WindowDecorationOptions> OverridesByType { get; }

    WindowDecorationOptions? GetDefaultForCategory(string category);
    WindowDecorationOptions? GetOverrideForType(string windowType);
    void SetDefaultForCategory(string category, WindowDecorationOptions options);
    bool RemoveDefaultForCategory(string category);
    void SetOverrideForType(string windowType, WindowDecorationOptions options);
    bool RemoveOverrideForType(string windowType);
    ValueTask<bool> SaveAsync(CancellationToken cancellationToken = default);
}

> **Service configuration**
> - Configuration file name: `Aura.json`
> - Configuration section name: `WindowDecorationSettings`
> - Service implemented via the Config module's `SettingsService<WindowDecorationSettings>` infrastructure backed by `IOptionsMonitor<WindowDecorationSettings>`
```

### JSON Serialization Support

```csharp
/// <summary>
/// Custom JSON converter for MenuOptions that stores only the provider ID.
/// </summary>
public sealed class MenuOptionsJsonConverter : JsonConverter<MenuOptions>
{
    public override MenuOptions? Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        if (reader.TokenType == JsonTokenType.Null)
            return null;

        using var doc = JsonDocument.ParseValue(ref reader);
        var root = doc.RootElement;

        var menuProviderId = root.GetProperty("menuProviderId").GetString();
        var isCompact = root.TryGetProperty("isCompact", out var compactProp)
            ? compactProp.GetBoolean()
            : false;

        if (string.IsNullOrWhiteSpace(menuProviderId))
            return null;

        return new MenuOptions
        {
            MenuProviderId = menuProviderId,
            IsCompact = isCompact
        };
    }

    public override void Write(Utf8JsonWriter writer, MenuOptions value, JsonSerializerOptions options)
    {
        writer.WriteStartObject();
        writer.WriteString("menuProviderId", value.MenuProviderId);
        writer.WriteBoolean("isCompact", value.IsCompact);
        writer.WriteEndObject();
    }
}

/// <summary>
/// Source generator context for AOT compilation and performance.
/// </summary>
[JsonSourceGenerationOptions(
    WriteIndented = true,
    PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase,
    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull)]
[JsonSerializable(typeof(WindowDecorationSettings))]
[JsonSerializable(typeof(WindowDecorationOptions))]
[JsonSerializable(typeof(TitleBarOptions))]
[JsonSerializable(typeof(WindowButtonsOptions))]
[JsonSerializable(typeof(MenuOptions))]
[JsonSerializable(typeof(BackdropKind))]
[JsonSerializable(typeof(DragRegionBehavior))]
[JsonSerializable(typeof(ButtonPlacement))]
internal partial class WindowDecorationJsonContext : JsonSerializerContext
{
}
```

### WindowContext Integration

```csharp
/// <summary>
/// Encapsulates metadata and state information for a managed window.
/// </summary>
/// <remarks>
/// WindowContext is a class (not a record) with required properties and mutable activation state.
/// The IsActive and LastActivatedAt properties are mutated in-place when windows are activated/deactivated.
/// Menu sources are created once per window by IWindowContextFactory and stored for the window's lifetime.
/// </remarks>
public sealed class WindowContext
{
    public required Guid Id { get; init; }
    public required Window Window { get; init; }
    public required WindowCategory Category { get; init; }
    public required string Title { get; init; }
    public required DateTimeOffset CreatedAt { get; init; }

    public WindowDecorationOptions? Decoration { get; init; }
    public IReadOnlyDictionary<string, object>? Metadata { get; init; }
    public IMenuSource? MenuSource { get; }

    // Mutable activation state
    public bool IsActive { get; private set; }
    public DateTimeOffset? LastActivatedAt { get; private set; }

    /// <summary>
    /// Updates the activation state of this window context (mutates in-place).
    /// </summary>
    public WindowContext WithActivationState(bool isActive);

    /// <summary>
    /// Sets the menu source for this window context (called by IWindowContextFactory).
    /// </summary>
    internal void SetMenuSource(IMenuSource menuSource);
}
```

### IAppearanceSettings Extension

```csharp
/// <summary>
/// Represents the appearance settings for an application.
/// </summary>
public interface IAppearanceSettings
{
    /// <summary>Gets or sets the app theme mode (Light, Dark, or Default).</summary>
    ElementTheme AppThemeMode { get; set; }

    /// <summary>Gets or sets the app theme background color as hexadecimal string.</summary>
    string AppThemeBackgroundColor { get; set; }

    /// <summary>Gets or sets the app theme font family.</summary>
    string AppThemeFontFamily { get; set; }

    /// <summary>
    /// Gets or sets the application-wide default backdrop effect.
    /// This backdrop is applied to all windows that do not have an explicit
    /// backdrop override in their WindowDecorationOptions.
    /// </summary>
    BackdropKind AppBackdrop { get; set; }
}
```

### WindowBackdropService

```csharp
/// <summary>
/// Service that manages backdrop application for windows based on their decoration settings.
/// Observes window lifecycle events and automatically applies backdrops when windows are created.
/// </summary>
public sealed class WindowBackdropService : IDisposable
{
    public WindowBackdropService(
        IWindowManagerService windowManager,
        ILoggerFactory? loggerFactory = null);

    /// <summary>
    /// Applies backdrop to a single window context based on WindowDecorationOptions.Backdrop.
    /// Uses WinUI 3 SystemBackdrop APIs (MicaBackdrop, DesktopAcrylicBackdrop).
    /// Logs warnings on failure; does not throw.
    /// </summary>
    /// <param name="context">The window context to apply backdrop to.</param>
    public void ApplyBackdrop(WindowContext context);

    /// <summary>
    /// Applies backdrops to all open windows.
    /// </summary>
    public void ApplyBackdrop();

    /// <summary>
    /// Applies backdrops to windows matching a predicate.
    /// </summary>
    /// <param name="predicate">Predicate to filter which windows should have backdrops applied.</param>
    public void ApplyBackdrop(Func<WindowContext, bool> predicate);

    public void Dispose();
}
```

### DI Registration Extension Methods

```csharp
/// <summary>
/// Extension methods for configuring window decoration services.
/// </summary>
public static class ServiceCollectionExtensions
{
    /// <summary>
    /// Adds window decoration services to the service collection.
    /// </summary>
    public static IServiceCollection AddWindowDecorationServices(this IServiceCollection services)
    {
        ArgumentNullException.ThrowIfNull(services);

        _ = services.AddSingleton<WindowDecorationSettingsService>();
        _ = services.AddSingleton<IWindowDecorationSettings>(
            sp => sp.GetRequiredService<WindowDecorationSettingsService>());
        _ = services.AddSingleton<ISettingsService<WindowDecorationSettings>>(
            sp => sp.GetRequiredService<WindowDecorationSettingsService>());

        _ = services.AddSingleton<WindowBackdropService>();

        return services;
    }

    /// <summary>
    /// Registers a menu provider factory for creating window menus.
    /// </summary>
    /// <param name="services">The service collection.</param>
    /// <param name="providerId">Unique identifier for the menu provider.</param>
    /// <param name="builderFactory">Factory function that creates MenuBuilder instances.</param>
    /// <returns>The service collection for chaining.</returns>
    /// <remarks>
    /// Use this overload when the menu structure is static and does not require DI services.
    /// For menu providers that need access to DI services, use the overload that accepts an IServiceProvider.
    /// </remarks>
    public static IServiceCollection AddMenuProvider(
        this IServiceCollection services,
        string providerId,
        Func<MenuBuilder> builderFactory)
    {
        ArgumentNullException.ThrowIfNull(services);
        ArgumentException.ThrowIfNullOrWhiteSpace(providerId);
        ArgumentNullException.ThrowIfNull(builderFactory);

        _ = services.AddSingleton<IMenuProvider>(new MenuProvider(providerId, builderFactory));

        return services;
    }

    /// <summary>
    /// Registers a scoped menu provider that resolves dependencies from DI.
    /// </summary>
    /// <param name="services">The service collection.</param>
    /// <param name="providerId">Unique identifier for the menu provider.</param>
    /// <param name="configure">Configuration action that receives MenuBuilder and IServiceProvider.</param>
    /// <returns>The service collection for chaining.</returns>
    /// <remarks>
    /// Use this overload when the menu structure depends on runtime services (e.g., current document state).
    /// The configure action is invoked each time a menu is created, with access to the DI container.
    /// </remarks>
    public static IServiceCollection AddMenuProvider(
        this IServiceCollection services,
        string providerId,
        Action<MenuBuilder, IServiceProvider> configure)
    {
        ArgumentNullException.ThrowIfNull(services);
        ArgumentException.ThrowIfNullOrWhiteSpace(providerId);
        ArgumentNullException.ThrowIfNull(configure);

        _ = services.AddSingleton<IMenuProvider>(
            sp => new ScopedMenuProvider(providerId, configure, sp));

        return services;
    }
}
```

> **Integration tip:** When composing the host (for example in `Program.cs`), ensure the decoration configuration file is included alongside existing entries: add `finder.GetConfigFilePath(WindowDecorationSettings.ConfigFileName)` to the configuration file list and call `services.Configure<WindowDecorationSettings>(configuration.GetSection(WindowDecorationSettings.ConfigSectionName))` before invoking `AddWindowDecorationServices()`.

## 5. UI Integration (Phase 11)

### Overview

Phase 11 integrates WindowDecorationOptions with MainShellView to create a fully data-driven window chrome system. All visual aspects (title bar height, icon visibility, menu rendering, button visibility) are controlled through XAML bindings to `WindowContext.Decoration` properties.

### Binding Architecture

**Data Flow:**

```
WindowDecorationOptions (immutable config)
    ↓
WindowContext.Decoration (stored during window creation)
    ↓
MainShellViewModel.Context (exposed property)
    ↓
MainShellView.xaml (XAML bindings)
    ↓
Rendered Chrome (title bar, menu, buttons)
```

**Key Principle:** MainShellView binds directly to `ViewModel.Context.Decoration.*` properties rather than duplicating state in MainShellViewModel. This ensures WindowContext remains the single source of truth.

### Menu Rendering Strategy

The system supports two menu presentation modes:

1. **Persistent Menu (MenuBar)**: Full-width horizontal menu bar, always visible
   - Used when `MenuOptions.IsCompact == false` or `Menu == null`
   - Suitable for Main, Document, Secondary windows
   - Bound to `WindowContext.MenuSource`

2. **Compact Menu (ExpandableMenuBar)**: Hamburger button that expands to full menu
   - Used when `MenuOptions.IsCompact == true`
   - Suitable for Tool, Transient windows with limited vertical space
   - State management: Collapsed ↔ Expanded
   - Bound to `WindowContext.MenuSource`

**Implementation:** Two controls in PrimaryCommands, visibility controlled by converters:

```xaml
<cm:MenuBar Visibility="{x:Bind Context.Decoration.Menu.IsCompact, Converter={StaticResource IsNotCompactToVis}}" />
<cm:ExpandableMenuBar Visibility="{x:Bind Context.Decoration.Menu.IsCompact, Converter={StaticResource IsCompactToVis}}" />
```

### Window Control Buttons

**Decision:** Use WinUI 3 native caption buttons, not custom controls.

**Rationale:**

- WinUI provides caption buttons automatically via `ExtendsContentIntoTitleBar = true`
- Native buttons respect Windows 11 design language (rounded corners, hover animations, theme)
- Positioned automatically based on system insets (RightPaddingColumn)
- No custom button implementation required in Phase 11

**Future Work (Phase 12+):**

- Custom button controls to support `ShowMinimize`, `ShowMaximize`, `ShowClose` properties
- `ButtonPlacement` (Left/Right/Auto) implementation
- Custom button styles and templates

**Current Behavior:**

- `ChromeEnabled = true` → Native caption buttons shown
- `ChromeEnabled = false` → System title bar with system buttons

### Title Bar Configuration

| Property | Binding Target | Behavior |
|----------|----------------|----------|
| `TitleBar.Height` | `CustomTitleBar.Height` | Sets title bar height in DIPs (device-independent pixels) |
| `TitleBar.Padding` | `PrimaryCommands.Margin` | Horizontal padding for menu and icon |
| `TitleBar.ShowIcon` | `ImageIcon.Visibility` | Shows/hides window icon (also serves as system menu trigger) |
| `TitleBar.ShowTitle` | *(Phase 12+)* | Shows/hides window title TextBlock (deferred) |
| `TitleBar.DragBehavior` | *(Phase 12+)* | Custom drag regions (current: Default behavior only) |

**Passthrough Regions:**

- `PrimaryCommands`: Menu bar interaction area (non-draggable)
- `SecondaryCommands`: Settings menu button (non-draggable)
- `DragColumn`: Main drag region (draggable)
- System calculates via `InputNonClientPointerSource.SetRegionRects()`

### Chrome Visibility

**ChromeEnabled Binding:**

```xaml
<Grid x:Name="CustomTitleBar"
      Visibility="{x:Bind Context.Decoration.ChromeEnabled, Converter={StaticResource BoolToVis}}">
```

**States:**

- `ChromeEnabled = true`:
  - CustomTitleBar visible
  - `Window.ExtendsContentIntoTitleBar = true`
  - Aura manages chrome, buttons, menu

- `ChromeEnabled = false`:
  - CustomTitleBar collapsed
  - `Window.ExtendsContentIntoTitleBar = false`
  - System provides standard title bar
  - Validation prevents Menu when ChromeEnabled=false

### Converters

**CommunityToolkit.WinUI.Converters Usage:**

Phase 11 exclusively uses converters from CommunityToolkit.WinUI.Converters - no custom converters are required:

**1. BoolToVisibilityConverter** (already in use)

- Converts boolean to Visibility
- Used for: ChromeEnabled, ShowIcon

**2. EmptyObjectToObjectConverter**

- Converts null/empty objects to specified values
- Configuration: `EmptyValue="Collapsed"`, `NotEmptyValue="Visible"`
- Used for: MenuOptions null checks (menu presence)
- Example:

```xaml
<ctkcvt:EmptyObjectToObjectConverter x:Key="NullToVis"
                                      EmptyValue="Collapsed"
                                      NotEmptyValue="Visible" />
```

**3. BoolToObjectConverter**

- Converts boolean to any object type (Visibility in our case)
- Two instances for IsCompact property handling:
  - **IsCompactToVis**: `TrueValue="Visible"`, `FalseValue="Collapsed"` (for ExpandableMenuBar)
  - **IsNotCompactToVis**: `TrueValue="Collapsed"`, `FalseValue="Visible"` (for MenuBar)
- Example:

```xaml
<ctkcvt:BoolToObjectConverter x:Key="IsCompactToVis"
                               TrueValue="Visible"
                               FalseValue="Collapsed" />
```

**Benefits:**

- Leverage well-tested, maintained toolkit converters
- No custom code to maintain
- Standard patterns recognized by WinUI developers
- Consistent with existing MainShellView (already uses BoolToVisibilityConverter)

### Settings Menu Independence

**Design Decision:** SettingsMenu (Settings/Themes flyout) remains independent of WindowContext.MenuSource.

**Rationale:**

- Application-level settings (theme, preferences) are always relevant
- Not tied to window-specific content
- Provides fallback UI for quick theme switching
- Adaptive visibility based on window width (existing behavior preserved)

**Binding:** `SecondaryCommands` binds to `ViewModel.SettingsMenu`, not `Context.MenuSource`

### Example Scenarios

**Main Window (Full Menu):**

```csharp
var decoration = WindowDecorationBuilder.ForMainWindow()
    .WithMenu("App.MainMenu", isCompact: false)
    .Build();
```

- CustomTitleBar: Visible, Height=40px
- Icon: Visible
- Menu: MenuBar (persistent), bound to MainMenu provider
- Buttons: All visible (native WinUI buttons)
- Backdrop: Mica

**Tool Window (Compact Menu):**

```csharp
var decoration = WindowDecorationBuilder.ForToolWindow()
    .WithMenu("App.ToolMenu", isCompact: true)
    .Build();
```

- CustomTitleBar: Visible, Height=32px
- Icon: Visible
- Menu: ExpandableMenuBar (hamburger)
- Buttons: Minimize/Close only (no maximize)
- Backdrop: MicaAlt

**System Window (No Chrome):**

```csharp
var decoration = new WindowDecorationOptions
{
    Category = WindowCategory.System,
    ChromeEnabled = false,
};
```

- CustomTitleBar: Collapsed
- System title bar shown by OS
- No custom chrome rendered

### Phase 11 Limitations

**Deferred to Future Phases:**

- Custom button controls (ShowMinimize/ShowMaximize/ShowClose)
- ButtonPlacement (Left/Right/Auto)
- Title TextBlock with ShowTitle binding
- DragRegionBehavior.Custom/Extended/None
- Per-window decoration overrides in settings
- Runtime decoration mutation

**Current Capabilities:**

- Title bar height configuration
- Icon visibility toggle
- Menu presence and compact mode
- Chrome on/off toggle
- Native WinUI button integration

See `plan/phase11-analysis.md` for detailed design rationale and implementation strategy.

## 6. Acceptance Criteria

### Preset Usage

- **AC-001**: Given a developer creates a tool window, When using `WindowDecorationBuilder.ForToolWindow().Build()`, Then the window shall have a 32px title bar, no maximize button, and no backdrop
- **AC-002**: Given a developer creates a primary window, When using `WindowDecorationBuilder.ForMainWindow().Build()`, Then the window shall have MicaAlt backdrop, all buttons visible, and default title bar height
- **AC-003**: Given a developer uses `WindowDecorationBuilder.WithSystemChromeOnly().Build()`, When the window is created, Then ChromeEnabled shall be false and system title bar shall be used

### Builder API

- **AC-004**: Given a developer starts with `WindowDecorationBuilder.ForToolWindow()`, When customizing properties, Then the builder shall preserve non-customized properties from the preset
- **AC-005**: Given a developer calls `Build()` on a builder, When the options are invalid, Then an ArgumentException shall be thrown with a clear error message
- **AC-006**: Given a developer uses fluent builder methods, When chaining multiple calls, Then all methods shall return the same builder instance for continued chaining

### Menu Integration

- **AC-007**: Given a menu provider ID is specified in options, When the window is created, Then the system shall resolve the provider from DI and create a menu source
- **AC-008**: Given a menu provider is not found, When the window is created, Then a warning shall be logged and the window shall have no menu without throwing
- **AC-009**: Given multiple windows use the same menu provider, When created concurrently, Then each window shall receive a distinct IMenuSource instance

### Persistence

- **AC-010**: Given decoration options are saved to settings, When loading settings, Then menu provider IDs shall be restored but menu sources shall not be persisted
- **AC-011**: Given settings contain invalid decoration options, When loading, Then Validate() shall throw ArgumentException after deserialization
- **AC-012**: Given user overrides for a window type, When creating that window type, Then the override shall take precedence over category defaults

### Validation

- **AC-013**: Given ChromeEnabled is false, When Menu is non-null, Then Validate() shall throw ArgumentException
- **AC-014**: Given a Main category window, When ShowClose is false, Then Validate() shall throw ArgumentException
- **AC-015**: Given title bar height is negative, When Validate() is called, Then ArgumentOutOfRangeException shall be thrown

### Backdrop Coordination

- **AC-016**: Given a window has WindowDecorationOptions.Backdrop set to a specific value (Mica, MicaAlt, Acrylic), When WindowBackdropService applies backdrop, Then the appropriate WinUI SystemBackdrop shall be set on the window
- **AC-017**: Given a window has WindowDecorationOptions with default Backdrop value (None), When WindowBackdropService applies backdrop, Then no backdrop shall be applied (Window.SystemBackdrop remains null)
- **AC-018**: Given BackdropKind.None is set explicitly, When WindowBackdropService applies backdrop, Then Window.SystemBackdrop shall be set to null
- **AC-019**: Given a WindowLifecycleEvent.Created is published, When WindowBackdropService observes the event, Then backdrop shall be automatically applied based on WindowContext.Decoration.Backdrop
- **AC-020**: Given a backdrop fails to apply due to exception, When WindowBackdropService handles the error, Then a warning shall be logged and the window shall remain functional with no backdrop

### WindowContext Integration

- **AC-021**: Given no explicit decoration is provided, When creating a window with windowType "Tool", Then the system shall infer WindowDecorationBuilder.ForToolWindow()
- **AC-022**: Given WindowContext is disposed, When menu source was created, Then the menu source shall be disposed to release resources

### Thread Safety

- **AC-023**: Given multiple windows are created concurrently, When resolving menu providers, Then no race conditions shall occur
- **AC-024**: Given MenuProvider.CreateMenuSource() is called concurrently, When using a thread-unsafe MenuBuilder, Then the provider shall use locking to ensure thread safety

## 6. Test Automation Strategy

### Test Levels

1. **Unit Tests**: Test individual classes and methods in isolation
   - WindowDecorationOptions validation logic
   - Builder pattern correctness
   - JSON serialization/deserialization round-trips
   - MenuOptionsJsonConverter correctness
   - Menu provider creation

2. **Integration Tests**: Test interactions between components
   - DI registration and resolution
   - WindowManagerService decoration resolution
   - Settings persistence and loading
   - WindowBackdropService automatic backdrop application on window creation events
   - WindowBackdropService predicate-based filtering and bulk application

3. **UI Tests**: Test visual and behavioral aspects
   - Title bar rendering with different heights and padding
   - Button visibility and placement
   - Menu display and compactness
   - Backdrop visual effects

### Frameworks

- **MSTest**: Primary testing framework for .NET
- **FluentAssertions**: For readable assertion syntax
- **Moq**: For mocking dependencies (IMenuProvider, IWindowDecorationSettings, ILogger)

### Test Data Management

- Use builder pattern to create test decoration options programmatically
- Use AutoFixture for generating test DTOs
- Create test menu providers with predictable provider IDs
- Mock IWindowDecorationSettings (or underlying file system) to avoid file I/O in unit tests
- Use in-memory collections for testing DI resolution

## 7. Rationale & Context

### Why Immutable Records?

Immutable records ensure thread safety, predictable state, and simplified testing. Once a WindowDecorationOptions instance is created, it cannot be modified, eliminating race conditions during concurrent window creation. Records also provide value-based equality, making comparisons straightforward.

### Why Menu Provider Abstraction?

Storing `IMenuSource` directly in `WindowDecorationOptions` would be problematic because `IMenuSource` contains mutable `ObservableCollection<MenuItemViewModel>`:

1. **Incompatible with immutable options** - Records cannot contain mutable collections
2. **Not serializable to JSON** - Commands and view models cannot be persisted
3. **Prone to shared state bugs** - Reusing instances across windows causes state conflicts

The `IMenuProvider` abstraction solves all three issues by:

- **Storing only a string ID in options** - Immutable, serializable provider reference
- **Creating fresh IMenuSource per window** - No shared mutable state between windows
- **Enabling DI resolution for testability** - Providers can be mocked in tests

### Why ScopedMenuProvider for DI Resolution?

The `MenuProvider` class uses closure-based factories, which can capture command references for the application lifetime. This creates memory management issues when commands hold references to heavyweight objects.

`ScopedMenuProvider` solves this by resolving dependencies from the DI container **at menu creation time**:

```csharp
// Commands resolved fresh per window creation
config.AddMenuProvider("App.MainMenu", (builder, sp) =>
{
    var fileCmd = sp.GetRequiredService<IFileMenuCommands>();
    builder.AddMenuItem("Save", fileCmd.SaveCommand);
});
```

**Important Design Note**: `ScopedMenuProvider` uses the **application-level DI container**, not a window-specific scope. This is intentional because:

1. Menu providers are **application-level configuration** registered once at startup
2. Menu structures (items, hierarchies) are typically **app-wide**, not per-window
3. The provider creates a **fresh IMenuSource instance** for each window without needing a dedicated window scope
4. Commands can still be **singleton, scoped, or transient** as appropriate in the application container

### Future Consideration: Window-Scoped DI Containers

While the current design uses application-level DI for menu providers, **window-scoped DI containers** could be valuable for:

- **Window-specific ViewModels** - State that should dispose with the window
- **Per-window data contexts** - Document-specific repositories or unit-of-work patterns
- **Window-scoped services** - Telemetry, logging, or feature toggles per window instance

If window-scoped DI becomes necessary, the architecture could evolve to:

1. Create an `IResolverContext` (DryIoc) or `IServiceScope` per WindowContext
2. Store the scope in WindowContext.Metadata for lifetime management
3. Resolve menu providers and other window-specific services from the window scope
4. Fall back to parent (application) resolver for singleton services

**Decision**: This level of scoping is **deferred to future phases** because:

- Current use cases don't require per-window service isolation
- Menu providers adequately solve the immediate lifetime management needs
- Adding window scopes adds complexity without clear current benefit (YAGNI)
- The design doesn't preclude adding window scopes later without breaking changes

### Why Preset-Based Design?

Research shows 90% of window decoration use cases fall into five categories: Main, Transient, Modal, Document, Tool, Secondary, and System. Presets eliminate boilerplate for common scenarios while fluent builders handle the remaining 10% of advanced cases.

### Why Direct Serialization with System.Text.Json?

The system uses direct serialization of `WindowDecorationOptions` records without intermediate DTOs:

```csharp
var settings = serviceProvider.GetRequiredService<IWindowDecorationSettings>();
settings.SetOverrideForType("ToolWindow", userPreference);
await settings.SaveAsync();
// SettingsService<T> serializes the snapshot directly via System.Text.Json
```

**Implementation approach:**

1. **Records serialize natively** - System.Text.Json has excellent support for C# records with init-only properties
2. **Custom JsonConverter for MenuOptions** - The `MenuOptionsJsonConverter` handles the special case where we only want to persist the `MenuProviderId` string, not the entire menu structure
3. **JsonSourceGenerationOptions** - AOT-friendly source generation for performance and trimming
4. **IOptionsMonitor integration** - `SettingsService<T>` hydrates from configuration and reacts to reload notifications
5. **Validation after deserialization** - Setter helpers validate through `WindowDecorationOptions.Validate()` before persistence

**Benefits of direct serialization:**

- ✅ Single source of truth (no DTO duplication)
- ✅ Compiler enforces schema consistency
- ✅ Less code to maintain (~150 lines of DTO code eliminated)
- ✅ AOT-compatible with source generators
- ✅ Better performance (no conversion overhead)
- ✅ Idiomatic .NET 8+ patterns

This approach works because `WindowDecorationOptions` contains only serializable primitives, enums, and nested records. The `IMenuProvider` reference is handled by the custom converter that stores only the provider ID string.

### Why No Runtime Mutation?

Allowing decoration changes during window lifetime introduces complexity:

- Title bar height changes require layout recalculation
- Menu source replacement requires disposal management
- Backdrop changes may fail mid-transition

Phase 4 intentionally scopes decorations to window creation time. Future phases may add controlled mutation APIs if justified by user demand.

### Why WindowBackdropService?

The `WindowBackdropService` provides automatic backdrop application by subscribing to window lifecycle events from `IWindowManagerService`. This centralizes backdrop coordination logic and ensures consistent behavior across the application.

**Event-Driven Architecture:**

- Service subscribes to `IWindowManagerService.WindowEvents` observable stream
- When `WindowLifecycleEvent.Created` is observed, applies backdrop automatically
- Eliminates need for explicit backdrop calls in window creation code
- Service lifecycle managed through IDisposable pattern

**Backdrop Application Strategy:**

1. **Read from WindowContext.Decoration.Backdrop**: Uses the BackdropKind value specified in window decoration options
2. **Default value is BackdropKind.None**: Windows without explicit backdrop specification have no backdrop applied
3. **Null decoration handled gracefully**: If WindowContext.Decoration is null, no backdrop is applied

**Bulk Application Support:**

- `ApplyBackdrop()` - Applies to all open windows
- `ApplyBackdrop(predicate)` - Applies to filtered windows (e.g., by category)
- Useful for theme changes or administrative window management

**Implementation Details:**

The service uses WinUI 3's `Window.SystemBackdrop` property with:

- `Microsoft.UI.Xaml.Media.MicaBackdrop` for `BackdropKind.Mica`
- `Microsoft.UI.Xaml.Media.MicaBackdrop` with `MicaKind.BaseAlt` for `BackdropKind.MicaAlt`
- `Microsoft.UI.Xaml.Media.DesktopAcrylicBackdrop` for `BackdropKind.Acrylic`
- `null` for `BackdropKind.None`

All three backdrop types are always available in WinUI 3, so no fallback logic is needed. Error handling wraps the application in try-catch to gracefully handle edge cases, logging warnings without throwing exceptions.

### Why Builder Pattern?

Nested object initializers for complex decorations become verbose:

```csharp
// Without builder - verbose
var options = new WindowDecorationOptions
{
    Category = "Custom",
    TitleBar = new TitleBarOptions { Height = 40, PaddingLeft = 20 },
    Buttons = new WindowButtonsOptions { ShowMaximize = false },
    // ...
};

// With builder - fluent and discoverable
var options = WindowDecorationBuilder
    .ForToolWindow()
    .WithTitleBar(tb => tb.WithHeight(40).WithPadding(20, 0))
    .WithButtons(b => b.HideMaximize())
    .Build();
```

Builders improve readability and enable progressive disclosure: start with a preset, customize incrementally.

### Why Integrate Presets into Builder?

Presets are integrated into `WindowDecorationBuilder` as factory methods rather than a separate class for several reasons:

1. **Single Entry Point** - Developers only need to discover `WindowDecorationBuilder`, not two separate types
2. **Better IntelliSense** - Typing `WindowDecorationBuilder.` shows all preset options immediately
3. **Natural Fluent Flow** - Preset factory methods return the builder, enabling immediate customization:

   ```csharp
   WindowDecorationBuilder.ForToolWindow()  // Returns builder
       .WithBackdrop(BackdropKind.Mica)     // Continues customization
       .Build();                             // Finalizes
   ```

4. **Fewer Types** - Eliminates the need for a separate preset factory class
5. **Consistency** - All decoration creation flows through a single API surface
6. **Discoverability** - Constants for menu provider IDs are co-located with preset methods that use them

This unified approach reduces cognitive load and makes the API more intuitive for both human developers and AI code generation.

## 8. Dependencies & External Integrations

### External Systems

- **EXT-001**: WinUI 3 Window API - Required for title bar customization, backdrop application, and system overlay
- **EXT-002**: Windows Runtime (WinRT) - Required for MicaBackdrop, DesktopAcrylicBackdrop system types

### Third-Party Services

- **SVC-001**: None (all functionality is local)

### Infrastructure Dependencies

- **INF-001**: .NET 8.0 Runtime - Required for record types, required properties, init-only properties
- **INF-002**: Dependency Injection Container - Must support IServiceProvider, IServiceCollection, singleton lifetimes
- **INF-003**: File System Access - Required for ISettingsService to load/save JSON settings to application data folder

### Data Dependencies

- **DAT-001**: `SettingsService<WindowDecorationSettings>` / `IWindowDecorationSettings` - Provides JSON persistence and domain access to decoration preferences via `IOptionsMonitor`
- **DAT-002**: IMenuSource (from Menus module) - Provides menu item data structures
- **DAT-003**: IAppearanceSettings (from Aura module) - Provides current theme mode and application-wide backdrop setting for backdrop coordination

### Technology Platform Dependencies

- **PLT-001**: WinUI 3 - Required for Window, TitleBar, SystemBackdrop APIs (version ≥ 1.4)
- **PLT-002**: .NET 8.0 - Required for record types, required properties, init-only setters
- **PLT-003**: C# 12 - Required for primary constructors, collection expressions

### Compliance Dependencies

- **COM-001**: None (no regulatory requirements)

### Module Dependencies

- **MOD-001**: Aura.WindowManagement - Provides WindowContext, IWindowManagerService
- **MOD-002**: DroidNet.Menus - Provides IMenuSource, MenuBuilder, MenuItemViewModel
- **MOD-003**: DroidNet.Config - Provides `ISettingsService<T>` for persistence
- **MOD-004**: DroidNet.Hosting - Provides DI container abstractions

## 9. Examples & Edge Cases

### Example 1: Simple Tool Window

```csharp
// Use preset as-is
var context = await windowManager.CreateWindowAsync<ToolWindow>(
    windowType: "Tool",
    title: "Properties",
    decoration: WindowDecorationBuilder.ForToolWindow().Build());
```

### Example 2: Document Window with Custom Menu

```csharp
// Register menu provider in DI (startup)
services.AddWindowDecoration()
    .AddMenuProvider("MyApp.DocMenu", () =>
    {
        var builder = new MenuBuilder();
        builder.AddMenuItem("Save", saveCommand);
        builder.AddMenuItem("Close", closeCommand);
        return builder;
    });

// Use preset with custom provider ID
var options = WindowDecorationBuilder
    .ForDocumentWindow("MyApp.DocMenu")
    .Build();

var context = await windowManager.CreateWindowAsync<DocumentWindow>(
    windowType: "Document",
    title: "Untitled.txt",
    decoration: options);
```

### Example 3: Customized Preset

```csharp
// Start from Tool preset and customize
var options = WindowDecorationBuilder
    .ForToolWindow("MyApp.ToolMenu")
    .WithBackdrop(BackdropKind.Mica)
    .WithTitleBar(tb => tb.WithHeight(40).WithPadding(10, 10))
    .Build();

var context = await windowManager.CreateWindowAsync<PropertyPanelWindow>(
    decoration: options);
```

### Example 4: Fully Custom Decoration

```csharp
var options = WindowDecorationBuilder
    .CreateNew()
    .WithCategory("CustomCategory")
    .WithMenu("MyApp.CustomMenu", isCompact: true)
    .WithBackdrop(BackdropKind.Acrylic)
    .WithTitleBar(tb => tb
        .WithHeight(50)
        .WithPadding(20, 20)
        .WithIcon(new Uri("ms-appx:///Assets/custom-icon.png")))
    .WithButtons(b => b
        .HideMaximize()
        .WithPlacement(ButtonPlacement.Left))
    .Build();

var context = await windowManager.CreateWindowAsync<CustomWindow>(decoration: options);
```

### Example 5: Persisting User Preferences

```csharp
// Save user's preferred decoration for ToolWindow type
var decorationSettings = serviceProvider.GetRequiredService<IWindowDecorationSettings>();

var userPreference = WindowDecorationBuilder
    .ForToolWindow()
    .WithBackdrop(BackdropKind.Mica)  // User prefers Mica for tools
    .Build();

decorationSettings.SetOverrideForType("ToolWindow", userPreference);
await decorationSettings.SaveAsync();

// Later: retrieve user preferences
var options = decorationSettings.GetOverrideForType("ToolWindow");
if (options is not null)
{
    var context = await windowManager.CreateWindowAsync<ToolWindow>(decoration: options);
}
```

### Example 6: DI Registration with Scoped Providers

```csharp
// In Program.cs or App.xaml.cs
services.AddWindowDecoration()
    .AddMenuProvider(WindowDecorationBuilder.PrimaryMenuProvider, (builder, sp) =>
    {
        // Commands resolved from DI per menu creation
        var fileCmd = sp.GetRequiredService<IFileMenuCommands>();
        builder.AddMenuItem("Open", fileCmd.OpenCommand);
        builder.AddMenuItem("Save", fileCmd.SaveCommand);
        builder.AddMenuItem("Exit", fileCmd.ExitCommand);
    })
    .AddMenuProvider(WindowDecorationBuilder.ToolMenuProvider, (builder, sp) =>
    {
        var toolCmd = sp.GetRequiredService<IToolCommands>();
        builder.AddMenuItem("Refresh", toolCmd.RefreshCommand);
    });
```

### Edge Case 1: Missing Menu Provider

```csharp
// Options specify non-existent provider
var options = WindowDecorationBuilder
    .ForDocumentWindow("NonExistent.MenuProvider")
    .Build();

// Result: Window created successfully, warning logged, no menu displayed
var context = await windowManager.CreateWindowAsync<DocumentWindow>(decoration: options);
// Log: "Warning: Menu provider 'NonExistent.MenuProvider' not found for window [ID]"
```

### Edge Case 2: Chrome Disabled with Menu

```csharp
// Invalid: ChromeEnabled=false but Menu specified
var options = WindowDecorationBuilder
    .WithSystemChromeOnly()
    .WithMenu("SomeMenu")  // This will fail validation
    .Build();
// Throws: ArgumentException("Menu cannot be specified when ChromeEnabled is false...")
```

### Edge Case 3: Main Window Without Close Button

```csharp
// Invalid: Main category must have Close button
var options = WindowDecorationBuilder
    .ForMainWindow()
    .WithButtons(b => b.HideClose())
    .Build();
// Throws: ArgumentException("Main window must have a Close button...")
```

### Edge Case 4: Concurrent Window Creation

```csharp
// Thread-safe menu provider resolution
var tasks = Enumerable.Range(0, 10).Select(async i =>
    await windowManager.CreateWindowAsync<ToolWindow>(
        title: $"Tool {i}",
        decoration: WindowDecorationBuilder.ForToolWindow().Build()));

await Task.WhenAll(tasks);
// Result: 10 distinct IMenuSource instances created, no race conditions
```

### Edge Case 5: Backdrop Application Failure

```csharp
var options = WindowDecorationBuilder
    .ForMainWindow()
    .WithBackdrop(BackdropKind.Acrylic)
    .Build();

var context = await windowManager.CreateWindowAsync<MainWindow>(decoration: options);

// If Acrylic fails to apply (e.g., unsupported GPU):
// Log: "Warning: Failed to apply backdrop Acrylic to window [ID]. Using fallback."
// Window: Functional with no backdrop, no crash
```

## 10. Validation Criteria

### Compile-Time Validation

- **VAL-001**: All public APIs must have XML documentation comments without warnings
- **VAL-002**: Required properties (Category, MenuProviderId) must be marked with `required` keyword
- **VAL-003**: All option types must be sealed records with init-only properties
- **VAL-004**: No mutable collections in option types

### Runtime Validation

- **VAL-005**: WindowDecorationOptions.Validate() must throw ArgumentException for empty Category
- **VAL-006**: Validate() must throw ArgumentException if ChromeEnabled=false and Menu is non-null
- **VAL-007**: Validate() must throw ArgumentException if ChromeEnabled=false and Backdrop is not None
- **VAL-008**: Validate() must throw ArgumentException if Menu is non-null and MenuProviderId is empty
- **VAL-009**: Validate() must throw ArgumentOutOfRangeException if TitleBar.Height is ≤ 0
- **VAL-010**: Validate() must throw ArgumentOutOfRangeException if TitleBar padding is negative
- **VAL-011**: Validate() must throw ArgumentException if Main category and ShowClose=false
- **VAL-012**: WindowDecorationBuilder.Build() must call Validate() before returning

### Functional Validation

- **VAL-013**: Preset factory methods (ForMainWindow, ForToolWindow, etc.) must produce valid options without requiring Validate() calls
- **VAL-014**: Builder fluent methods must return the same builder instance for chaining
- **VAL-015**: Builder preset factory methods must initialize all properties to valid defaults
- **VAL-016**: WindowDecorationOptions must serialize to JSON and deserialize correctly using System.Text.Json
- **VAL-017**: Serialization round-trip (serialize then deserialize) must produce equivalent options

### Integration Validation

- **VAL-018**: DI-registered menu providers must be resolvable via `IEnumerable<IMenuProvider>`
- **VAL-019**: WindowManagerService must apply decoration resolution in order: explicit → registry → inference
- **VAL-020**: WindowBackdropService must not throw exceptions on backdrop application failure
- **VAL-021**: Missing menu providers must log warnings without throwing

### Performance Validation

- **VAL-022**: Decoration resolution must complete in ≤ 2ms (P95)
- **VAL-023**: Menu source creation must complete in ≤ 10ms (P95)
- **VAL-024**: 10 concurrent window creations must succeed without errors

## 11. Related Specifications / Further Reading

### Internal Specifications

- **Multi-Window Management Specification** (MULTI_WINDOW_IMPLEMENTATION.md) - Foundation for WindowContext and IWindowManagerService
- **Config Module Specification** - `ISettingsService<T>` pattern for persistence
- **Menus Module Specification** - IMenuSource and MenuBuilder API

### External Documentation

- [WinUI 3 Title Bar Customization](https://learn.microsoft.com/en-us/windows/apps/develop/title-bar) - Official Microsoft documentation for title bar APIs
- [SystemBackdrop Overview](https://learn.microsoft.com/en-us/windows/apps/design/style/mica) - Mica and Acrylic backdrop documentation
- [C# Records](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/builtin-types/record) - Language reference for record types
- [Fluent Builder Pattern](https://refactoring.guru/design-patterns/builder) - General pattern documentation

### Design Evolution Documents

- CLAUDE_DECORATION.md (Iterations 1-4) - Detailed design evolution and rationale
- window-decorations-spec.md (this document) - Formal specification derived from design iterations

---

**Document Status**: Ready for Implementation
**Next Phase**: Implementation (TASK-010 through TASK-017)
**Review Cadence**: Update after each iteration feedback
