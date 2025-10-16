---
goal: Implement Aura Window Decoration System (Phase 4)
version: 1.0
date_created: 2025-01-16
last_updated: 2025-01-16
owner: Aura Module Team
status: 'Planned'
tags: [feature, aura, window-management, ui, architecture]
---

# Introduction

![Status: Planned](https://img.shields.io/badge/status-Planned-blue)

This implementation plan defines the complete development roadmap for the Aura Window Decoration System (Phase 4), a strongly-typed, immutable, and persistable window decoration framework. The system provides preset-based defaults, fluent builder APIs, type-safe menu integration, and seamless persistence while maintaining backward compatibility with existing Aura modules.

## 1. Requirements & Constraints

### Requirements

- **REQ-001**: Implement WindowDecorationOptions immutable record with Category, ChromeEnabled, TitleBar, Buttons, Menu, Backdrop, and EnableSystemTitleBarOverlay properties
- **REQ-002**: Implement five built-in presets: Primary, Document, Tool, Secondary, and SystemChrome
- **REQ-003**: Each preset must have sensible defaults suitable for 90% of use cases in that category
- **REQ-004**: Implement fluent WindowDecorationBuilder API for creating custom decorations
- **REQ-005**: Builder API must support starting from a preset and customizing specific properties
- **REQ-006**: Integrate with Menus module via IMenuProvider abstraction
- **REQ-007**: IMenuProvider must create new IMenuSource instances per window to avoid shared mutable state
- **REQ-008**: Persist decoration preferences via WindowDecorationSettings and `ISettingsService<T>`
- **REQ-009**: Use System.Text.Json for serialization with custom converters for non-serializable properties
- **REQ-010**: Menu providers must not be persisted; only provider IDs stored via MenuOptionsJsonConverter
- **REQ-011**: Validate decoration options and throw clear ArgumentException for invalid combinations
- **REQ-012**: WindowContext must include a nullable Decoration property
- **REQ-013**: WindowManagerService must resolve decorations via explicit parameter, registry lookup, or type inference
- **REQ-014**: Implement WindowBackdropService to coordinate backdrop application with theme settings
- **REQ-015**: Support graceful degradation when menu providers are not found
- **REQ-016**: Allow opting out of Aura chrome via ChromeEnabled=false
- **REQ-017**: WindowDecorationOptions must be immutable after creation (record with init-only properties)
- **REQ-018**: Provide extension methods for DI registration of decoration services and menu providers
- **REQ-019**: Menu provider creation must be thread-safe for concurrent window activation
- **REQ-020**: Log warnings for missing menu providers without throwing exceptions

### Security Requirements

- **SEC-001**: Menu providers must be resolved only from DI container, not from external plugin files
- **SEC-002**: Settings must be loaded only from trusted application data folder
- **SEC-003**: Type resolution must not use reflection on user-provided string type names
- **SEC-004**: Validation must prevent malicious option combinations (e.g., negative padding, zero height)
- **SEC-005**: Menu provider IDs must be validated as non-empty strings

### Performance Requirements

- **PER-001**: Decoration resolution and validation must complete in ≤ 2ms per window creation
- **PER-002**: Menu provider resolution must be O(n) where n is the number of registered providers (typically < 10)
- **PER-003**: Menu source creation must occur once per window, not per activation
- **PER-004**: Settings deserialization must be lazy and on-demand, not in window creation hot path

### Constraints

- **CON-001**: WindowDecorationOptions and nested option types must be immutable records
- **CON-002**: No breaking changes to existing IWindowManagerService API are permitted
- **CON-003**: Menu providers must implement IMenuProvider with a string ProviderId and CreateMenuSource method
- **CON-004**: Decoration options must not store mutable IMenuSource references (only provider IDs)
- **CON-005**: Must not introduce dependencies on third-party serialization libraries
- **CON-006**: All public APIs must have XML documentation comments
- **CON-007**: Decorations are bound once per window lifetime; runtime mutation is not supported
- **CON-008**: Must integrate with existing Config module's `ISettingsService<T>` pattern
- **CON-009**: Backdrop application must respect AppearanceSettingsService theme mode
- **CON-010**: Primary windows must have a Close button to ensure proper application shutdown

### Guidelines

- **GUD-001**: Use presets for simple cases; use builders for advanced customization
- **GUD-002**: Register menu providers in DI startup using ServiceCollectionExtensions
- **GUD-003**: Prefer explicit decoration parameters over registry-based resolution for clarity
- **GUD-004**: Use with expressions for small customizations to presets
- **GUD-005**: Validate custom decorations early by calling Build() on builders
- **GUD-006**: Log decoration resolution at Information level for debugging window creation issues
- **GUD-007**: Use null for MenuOptions to indicate no menu should be displayed
- **GUD-008**: Use BackdropKind.None to explicitly disable backdrop effects
- **GUD-009**: Dispose WindowContext instances to release menu sources
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

## 2. Implementation Steps

### Phase 1: Core Data Structures and Enumerations

- GOAL-001: Establish immutable record types and enumerations for window decoration configuration

| Completed | Task | Description |
|-----------|------|-------------|
| | TASK-001 | Create `BackdropKind` enum in `projects/Aura/src/Decoration/BackdropKind.cs` with values: None, Mica, MicaAlt, Acrylic |
| | TASK-002 | Create `DragRegionBehavior` enum in `projects/Aura/src/Decoration/DragRegionBehavior.cs` with values: Default, Extended, Custom, None |
| | TASK-003 | Create `ButtonPlacement` enum in `projects/Aura/src/Decoration/ButtonPlacement.cs` with values: Left, Right, Auto |
| | TASK-004 | Create `TitleBarOptions` record in `projects/Aura/src/Decoration/TitleBarOptions.cs` with properties: Height (32.0), Padding (8.0), ShowTitle (true), ShowIcon (true), DragBehavior (Default) |
| | TASK-005 | Create `WindowButtonsOptions` record in `projects/Aura/src/Decoration/WindowButtonsOptions.cs` with properties: ShowMinimize (true), ShowMaximize (true), ShowClose (true), Placement (Right) |
| | TASK-006 | Create `MenuOptions` record in `projects/Aura/src/Decoration/MenuOptions.cs` with properties: MenuProviderId (required string), IsCompact (false) and [JsonConverter(typeof(MenuOptionsJsonConverter))] attribute |
| | TASK-007 | Add XML documentation comments to all enums and records following CON-006 |
| | TASK-008 | Add static `Default` property to TitleBarOptions and WindowButtonsOptions for easy reuse |

### Phase 2: WindowDecorationOptions and Validation

- GOAL-002: Implement the core WindowDecorationOptions record with comprehensive validation logic

| Completed | Task | Description |
|-----------|------|-------------|
| | TASK-009 | Create `WindowDecorationOptions` record in `projects/Aura/src/Decoration/WindowDecorationOptions.cs` with properties: Category (required), ChromeEnabled (true), TitleBar (Default), Buttons (Default), Menu (nullable), Backdrop (None), EnableSystemTitleBarOverlay (false) |
| | TASK-010 | Implement `Validate()` method to check: ChromeEnabled=false excludes Menu (REQ-011), Primary category requires ShowClose=true (CON-010), TitleBar.Height > 0, TitleBar.Padding >= 0 |
| | TASK-011 | Add validation for invalid combinations and throw ArgumentException with clear messages (SEC-004) |
| | TASK-012 | Add XML documentation to all properties and methods |
| | TASK-013 | Write unit tests in `projects/Aura/tests/Decoration/WindowDecorationOptionsTests.cs` covering: valid options pass validation, ChromeEnabled=false with Menu throws, Primary without Close throws, negative height/padding throws |

### Phase 3: Menu Provider Abstraction

- GOAL-003: Implement IMenuProvider abstraction for thread-safe, per-window menu source creation

| Completed | Task | Description |
|-----------|------|-------------|
| | TASK-014 | Create `IMenuProvider` interface in `projects/Aura/src/Decoration/IMenuProvider.cs` with properties: ProviderId (string), and method: CreateMenuSource() returning IMenuSource |
| | TASK-015 | Create `MenuProvider` class in `projects/Aura/src/Decoration/MenuProvider.cs` implementing IMenuProvider with constructor: MenuProvider(string providerId, `Func<MenuBuilder>` builderFactory) |
| | TASK-016 | Implement thread-safe CreateMenuSource() in MenuProvider using lock on builderFactory invocation (REQ-019) |
| | TASK-017 | Create `ScopedMenuProvider` class in `projects/Aura/src/Decoration/ScopedMenuProvider.cs` implementing IMenuProvider with constructor: ScopedMenuProvider(string providerId, IServiceProvider serviceProvider, Action<MenuBuilder, IServiceProvider> configureMenu) |
| | TASK-018 | Implement CreateMenuSource() in ScopedMenuProvider to resolve MenuBuilder from DI, invoke configureMenu action, and build menu source |
| | TASK-019 | Add XML documentation to IMenuProvider, MenuProvider, and ScopedMenuProvider |
| | TASK-020 | Write unit tests in `projects/Aura/tests/Decoration/MenuProviderTests.cs` covering: MenuProvider thread safety with concurrent CreateMenuSource calls, ScopedMenuProvider DI resolution, provider ID validation |

### Phase 4: Fluent Builder API

- GOAL-004: Implement WindowDecorationBuilder with preset factory methods and fluent customization

| Completed | Task | Description |
|-----------|------|-------------|
| | TASK-021 | Create `WindowDecorationBuilder` class in `projects/Aura/src/Decoration/WindowDecorationBuilder.cs` with private fields for all WindowDecorationOptions properties |
| | TASK-022 | Implement static preset factory methods: ForPrimaryWindow() (MicaAlt backdrop, all buttons, 40px title bar), ForDocumentWindow() (Mica backdrop, all buttons, standard title bar), ForToolWindow() (no backdrop, no maximize, 32px title bar), ForSecondaryWindow() (no backdrop, all buttons), WithSystemChromeOnly() (ChromeEnabled=false) |
| | TASK-023 | Add fluent methods: WithCategory(string), WithChrome(bool), WithTitleBar(TitleBarOptions), WithButtons(WindowButtonsOptions), WithMenu(MenuOptions), WithBackdrop(BackdropKind), WithSystemTitleBarOverlay(bool) - all returning `this` |
| | TASK-024 | Add shorthand fluent methods: WithMenu(string providerId, bool isCompact = false), WithTitleBarHeight(double height), NoMaximize(), NoMinimize(), NoBackdrop() |
| | TASK-025 | Implement Build() method that constructs WindowDecorationOptions, calls Validate(), and returns immutable instance |
| | TASK-026 | Add MenuProviderIds static class with constants: "App.MainMenu", "App.ContextMenu", "App.ToolMenu" |
| | TASK-027 | Add XML documentation to all builder methods with usage examples |
| | TASK-028 | Write unit tests in `projects/Aura/tests/Decoration/WindowDecorationBuilderTests.cs` covering: all presets build valid options, fluent customization preserves preset defaults, Build() validates and throws on invalid state, method chaining works correctly |

### Phase 5: JSON Serialization Support

- GOAL-005: Implement System.Text.Json serialization with custom converters for non-serializable properties

| Completed | Task | Description |
|-----------|------|-------------|
| | TASK-029 | Create `MenuOptionsJsonConverter` class in `projects/Aura/src/Decoration/Serialization/MenuOptionsJsonConverter.cs` inheriting from `JsonConverter<MenuOptions>` |
| | TASK-030 | Implement Read() method to deserialize JSON object with "menuProviderId" and "isCompact" properties into MenuOptions record |
| | TASK-031 | Implement Write() method to serialize MenuOptions as JSON object with only providerId and isCompact (REQ-010) |
| | TASK-032 | Create `WindowDecorationJsonContext` partial class in `projects/Aura/src/Decoration/Serialization/WindowDecorationJsonContext.cs` with [JsonSourceGenerationOptions] attributes and [JsonSerializable] for all decoration types |
| | TASK-033 | Configure JsonSerializerOptions: WriteIndented=true, PropertyNamingPolicy=CamelCase, DefaultIgnoreCondition=WhenWritingNull |
| | TASK-034 | Add XML documentation to converter classes |
| | TASK-035 | Write unit tests in `projects/Aura/tests/Decoration/SerializationTests.cs` covering: round-trip serialization of WindowDecorationOptions, MenuOptions serializes only providerId, enums serialize as strings, null properties are omitted |

### Phase 6: WindowDecorationSettings and Persistence

- GOAL-006: Implement persistent settings for window decoration preferences using `ISettingsService<T>`

| Completed | Task | Description |
|-----------|------|-------------|
| | TASK-036 | Create `WindowDecorationSettings` class in `projects/Aura/src/Decoration/WindowDecorationSettings.cs` with properties: DefaultsByCategory (Dictionary<string, WindowDecorationOptions>), OverridesByType (Dictionary<string, WindowDecorationOptions>) |
| | TASK-037 | Add [JsonSourceGenerationOptions(WriteIndented = true)] attribute to WindowDecorationSettings |
| | TASK-038 | Implement default constructor that initializes empty dictionaries |
| | TASK-039 | Create `WindowDecorationSettingsService` class in `projects/Aura/src/Decoration/WindowDecorationSettingsService.cs` wrapping `ISettingsService<WindowDecorationSettings>` |
| | TASK-040 | Add methods: GetDefaultForCategory(string category), GetOverrideForType(string windowType), SetDefaultForCategory(string category, WindowDecorationOptions options), SetOverrideForType(string windowType, WindowDecorationOptions options), SaveAsync(), LoadAsync() |
| | TASK-041 | Implement lazy loading pattern: settings loaded on first access, cached thereafter (PER-004) |
| | TASK-042 | Add XML documentation to WindowDecorationSettings and WindowDecorationSettingsService |
| | TASK-043 | Write unit tests in `projects/Aura/tests/Decoration/WindowDecorationSettingsTests.cs` covering: save and load round-trip, default category lookup, override by type lookup, lazy loading behavior |

### Phase 7: WindowBackdropService

- GOAL-007: Implement backdrop coordinator service that applies backdrop effects with theme integration

| Completed | Task | Description |
|-----------|------|-------------|
| | TASK-044 | Create `WindowBackdropService` class in `projects/Aura/src/Decoration/WindowBackdropService.cs` with constructor dependencies: `ILogger<WindowBackdropService>`, IAppearanceSettingsService |
| | TASK-045 | Implement ApplyBackdrop(Window window, BackdropKind requested, WindowContext context) method |
| | TASK-046 | Add logic to check BackdropKind.None and skip application (REQ-015) |
| | TASK-047 | Add logic to query IAppearanceSettingsService for current theme mode and select appropriate backdrop variant (CON-009) |
| | TASK-048 | Add platform capability checks: try Mica first, fall back to MicaAlt, then Acrylic, log warnings on unsupported backdrops (REQ-015) |
| | TASK-049 | Wrap backdrop application in try-catch, log errors, allow window to continue without backdrop on failure (AC-018) |
| | TASK-050 | Add XML documentation with examples |
| | TASK-051 | Write unit tests in `projects/Aura/tests/Decoration/WindowBackdropServiceTests.cs` covering: BackdropKind.None skips application, theme mode affects backdrop selection, unsupported backdrop logs warning, exception handling allows graceful degradation |

### Phase 8: WindowContext Integration

- GOAL-008: Extend WindowContext to include decoration options and enable disposal of menu sources

| Completed | Task | Description |
|-----------|------|-------------|
| | TASK-052 | Add `Decoration` property (WindowDecorationOptions?) to WindowContext record in `projects/Aura/src/WindowManagement/WindowContext.cs` (REQ-012) |
| | TASK-053 | Add private `_menuSource` field (IMenuSource?) to track created menu source for disposal |
| | TASK-054 | Update WindowContext.Create() factory method to accept optional WindowDecorationOptions parameter |
| | TASK-055 | If decoration has MenuOptions, resolve IMenuProvider from DI, call CreateMenuSource(), store in _menuSource field |
| | TASK-056 | Implement IDisposable.Dispose() method to dispose _menuSource if present (GUD-009) |
| | TASK-057 | Add logging at Information level when menu source is created and disposed |
| | TASK-058 | Add XML documentation to Decoration property and updated Create() method |
| | TASK-059 | Write unit tests in `projects/Aura/tests/WindowManagement/WindowContextTests.cs` covering: Decoration property is set correctly, menu source created from provider, Dispose releases menu source, missing menu provider logs warning without throwing (REQ-020) |

### Phase 9: WindowManagerService Decoration Resolution

- GOAL-009: Integrate decoration resolution into WindowManagerService for explicit, registry, and type-based lookup

| Completed | Task | Description |
|-----------|------|-------------|
| | TASK-060 | Add `WindowDecorationSettingsService` dependency to WindowManagerService constructor in `projects/Aura/src/WindowManagement/WindowManagerService.cs` |
| | TASK-061 | Update existing window creation methods to accept optional WindowDecorationOptions parameter (CON-002: no breaking changes, parameter is optional) |
| | TASK-062 | Implement decoration resolution logic: (1) Use explicit parameter if provided, (2) Look up in settings by window type, (3) Look up in settings by category, (4) Infer from window type string (e.g., "Tool" -> ForToolWindow()), (5) Fall back to WithSystemChromeOnly() (REQ-013) |
| | TASK-063 | Log decoration resolution at Information level with source (explicit/registry/inferred/fallback) (GUD-006) |
| | TASK-064 | Pass resolved decoration to WindowContext.Create() |
| | TASK-065 | Ensure thread-safe concurrent window creation with decoration resolution (REQ-019) |
| | TASK-066 | Add XML documentation to updated methods |
| | TASK-067 | Write integration tests in `projects/Aura/tests/WindowManagement/WindowManagerServiceDecorationTests.cs` covering: explicit decoration used, registry override used, type inference works, fallback to system chrome, concurrent window creation |

### Phase 10: DI Registration Extensions

- GOAL-010: Provide extension methods for registering decoration services and menu providers in DI container

| Completed | Task | Description |
|-----------|------|-------------|
| | TASK-068 | Create `ServiceCollectionExtensions` class in `projects/Aura/src/Decoration/ServiceCollectionExtensions.cs` |
| | TASK-069 | Implement AddWindowDecorationServices(this IServiceCollection services) extension method |
| | TASK-070 | Register WindowBackdropService as singleton in AddWindowDecorationServices |
| | TASK-071 | Register WindowDecorationSettingsService as singleton wrapping `ISettingsService<WindowDecorationSettings>` |
| | TASK-072 | Implement AddMenuProvider(this IServiceCollection services, string providerId, `Func<MenuBuilder>` builderFactory) extension method |
| | TASK-073 | Register MenuProvider instance as singleton IMenuProvider in AddMenuProvider |
| | TASK-074 | Implement AddScopedMenuProvider(this IServiceCollection services, string providerId, Action<MenuBuilder, IServiceProvider> configureMenu) extension method |
| | TASK-075 | Register ScopedMenuProvider instance as singleton IMenuProvider in AddScopedMenuProvider |
| | TASK-076 | Add XML documentation to all extension methods with usage examples (GUD-002) |
| | TASK-077 | Write integration tests in `projects/Aura/tests/Decoration/ServiceCollectionExtensionsTests.cs` covering: services registered correctly, menu providers resolvable from `IEnumerable<IMenuProvider>`, scoped providers resolve dependencies |

### Phase 11: XAML Binding and UI Integration

- GOAL-011: Update MainShellView XAML to bind window decoration properties to UI elements

| Completed | Task | Description |
|-----------|------|-------------|
| | TASK-078 | Locate MainShellView.xaml in `projects/Aura/src/Views/MainShellView.xaml` |
| | TASK-079 | Add DataContext binding to WindowContext in MainShellView code-behind |
| | TASK-080 | Add title bar Grid with Height binding to Decoration.TitleBar.Height |
| | TASK-081 | Add title bar content: icon (Visibility bound to Decoration.TitleBar.ShowIcon), title TextBlock (Visibility bound to Decoration.TitleBar.ShowTitle) |
| | TASK-082 | Add drag region Rectangle with margin based on Decoration.TitleBar.DragBehavior |
| | TASK-083 | Add menu ContentControl with ItemsSource binding to menu source (Visibility bound to Decoration.Menu != null), IsCompact style trigger based on Decoration.Menu.IsCompact |
| | TASK-084 | Add window buttons StackPanel with HorizontalAlignment bound to Decoration.Buttons.Placement (Left/Right) |
| | TASK-085 | Add minimize button (Visibility bound to Decoration.Buttons.ShowMinimize), maximize button (Visibility bound to Decoration.Buttons.ShowMaximize), close button (Visibility bound to Decoration.Buttons.ShowClose) |
| | TASK-086 | Add converter for ButtonPlacement to HorizontalAlignment in `projects/Aura/src/Converters/ButtonPlacementToAlignmentConverter.cs` |
| | TASK-087 | Write UI tests in `projects/Aura/tests/Views/MainShellViewTests.cs` covering: title bar renders with correct height, buttons visibility matches options, menu renders when provider specified, drag region behavior correct |

### Phase 12: Documentation and Examples

- GOAL-012: Create comprehensive documentation and example implementations for developers

| Completed | Task | Description |
|-----------|------|-------------|
| | TASK-088 | Create `projects/Aura/docs/WindowDecoration.md` with overview of decoration system |
| | TASK-089 | Document all five presets with use cases and visual examples |
| | TASK-090 | Add code examples for basic preset usage: `WindowDecorationBuilder.ForPrimaryWindow().Build()` |
| | TASK-091 | Add code examples for builder customization: starting from preset and using fluent methods |
| | TASK-092 | Add code examples for menu provider registration in Startup.cs or Program.cs |
| | TASK-093 | Add code examples for persistence: saving/loading decoration preferences |
| | TASK-094 | Document validation rules and common error messages |
| | TASK-095 | Create sample application in `tooling/samples/WindowDecorationSample/` demonstrating all presets and customization patterns |
| | TASK-096 | Add README.md to sample application with build and run instructions |

### Phase 13: Testing and Quality Assurance

- GOAL-013: Achieve comprehensive test coverage and validate all acceptance criteria

| Completed | Task | Description |
|-----------|------|-------------|
| | TASK-097 | Run all unit tests and achieve ≥90% code coverage for decoration classes |
| | TASK-098 | Validate AC-001: Tool window preset has 32px title bar, no maximize button, no backdrop |
| | TASK-099 | Validate AC-002: Primary window preset has MicaAlt backdrop, all buttons, default title bar |
| | TASK-100 | Validate AC-003: SystemChromeOnly preset sets ChromeEnabled=false |
| | TASK-101 | Validate AC-004: Builder preserves non-customized properties from preset |
| | TASK-102 | Validate AC-005: Build() throws ArgumentException on invalid options |
| | TASK-103 | Validate AC-006: Fluent methods return same builder for chaining |
| | TASK-104 | Validate AC-007: Menu provider resolved from DI and creates menu source |
| | TASK-105 | Validate AC-008: Missing menu provider logs warning without throwing |
| | TASK-106 | Validate AC-009: Concurrent windows receive distinct menu source instances |
| | TASK-107 | Validate AC-010: Menu provider IDs restored from settings, sources not persisted |
| | TASK-108 | Validate AC-011: Validate() throws on invalid settings after deserialization |
| | TASK-109 | Validate AC-012: User override takes precedence over category default |
| | TASK-110 | Validate AC-013: ChromeEnabled=false with Menu throws ArgumentException |
| | TASK-111 | Validate AC-014: Primary category without ShowClose throws ArgumentException |
| | TASK-112 | Validate AC-015: Negative title bar height throws ArgumentOutOfRangeException |
| | TASK-113 | Validate AC-016: BackdropKind.None results in no backdrop applied |
| | TASK-114 | Validate AC-017: Unsupported backdrop logs warning and falls back gracefully |
| | TASK-115 | Validate AC-018: Backdrop failure leaves window functional with no backdrop |
| | TASK-116 | Validate AC-019: Type "Tool" infers ForToolWindow() decoration |
| | TASK-117 | Validate AC-020: WindowContext.Dispose() releases menu source |
| | TASK-118 | Validate AC-021: No race conditions in concurrent window creation |
| | TASK-119 | Validate AC-022: MenuProvider.CreateMenuSource() is thread-safe |
| | TASK-120 | Validate PER-001: Decoration resolution completes in ≤2ms per window |
| | TASK-121 | Validate PER-002: Menu provider resolution is O(n) with n < 10 providers |

### Phase 14: Integration and Migration

- GOAL-014: Integrate decoration system with existing Aura applications and provide migration guidance

| Completed | Task | Description |
|-----------|------|-------------|
| | TASK-122 | Update Aura module DI registration to call AddWindowDecorationServices() |
| | TASK-123 | Update existing Aura sample applications to use decoration system |
| | TASK-124 | Create migration guide in `projects/Aura/docs/Migration-WindowDecoration.md` for existing users |
| | TASK-125 | Document breaking changes (none expected per CON-002) and new optional features |
| | TASK-126 | Add troubleshooting section for common integration issues |
| | TASK-127 | Update Aura README.md with decoration system overview and quick start |
| | TASK-128 | Validate backward compatibility: existing code without decorations continues to work |

## 3. Alternatives

- **ALT-001**: Store IMenuSource directly in WindowDecorationOptions - Rejected because IMenuSource contains mutable ObservableCollection, incompatible with immutable records and not serializable
- **ALT-002**: Use mutable classes instead of immutable records - Rejected because it introduces thread-safety issues during concurrent window creation and makes testing more complex
- **ALT-003**: Use XML serialization instead of System.Text.Json - Rejected to avoid third-party dependencies (CON-005) and leverage native .NET serialization performance
- **ALT-004**: Allow runtime mutation of decorations - Rejected because it complicates state management and was deferred to future phases per spec scope
- **ALT-005**: Use string-based type names for menu providers - Rejected due to security concerns (SEC-003) and to enforce DI-only resolution (SEC-001)
- **ALT-006**: Create window-scoped DI containers for menu providers - Deferred to future consideration; current design uses application-level container with fresh menu source instances per window
- **ALT-007**: Hard-code preset configurations - Rejected in favor of factory methods on WindowDecorationBuilder to maintain flexibility and discoverability

## 4. Dependencies

- **DEP-001**: Aura.Menus module must provide IMenuSource with `ObservableCollection<MenuItemViewModel>` and MenuBuilder for creating menu hierarchies
- **DEP-002**: Aura.Config module must provide `ISettingsService<T>` interface with SaveAsync/LoadAsync methods for JSON persistence
- **DEP-003**: Aura.AppearanceSettingsService must expose current theme mode (Light/Dark/System) for backdrop coordination
- **DEP-004**: WinUI 3 Microsoft.UI.Xaml.Window must support backdrop APIs (SetBackdropConfiguration, MicaController, DesktopAcrylicController)
- **DEP-005**: System.Text.Json must support source generators for AOT compilation and JsonConverter for custom serialization
- **DEP-006**: Microsoft.Extensions.DependencyInjection must be available for DI container registration
- **DEP-007**: Microsoft.Extensions.Logging must be available for logging decoration resolution and errors
- **DEP-008**: Aura.WindowManagerService must be injectable and support optional decoration parameters

## 5. Files

- **FILE-001**: `projects/Aura/src/Decoration/BackdropKind.cs` - Enum for backdrop material kinds (None, Mica, MicaAlt, Acrylic)
- **FILE-002**: `projects/Aura/src/Decoration/DragRegionBehavior.cs` - Enum for title bar drag behaviors
- **FILE-003**: `projects/Aura/src/Decoration/ButtonPlacement.cs` - Enum for window button placement
- **FILE-004**: `projects/Aura/src/Decoration/TitleBarOptions.cs` - Immutable record for title bar configuration
- **FILE-005**: `projects/Aura/src/Decoration/WindowButtonsOptions.cs` - Immutable record for button configuration
- **FILE-006**: `projects/Aura/src/Decoration/MenuOptions.cs` - Immutable record for menu configuration
- **FILE-007**: `projects/Aura/src/Decoration/WindowDecorationOptions.cs` - Main immutable record with validation
- **FILE-008**: `projects/Aura/src/Decoration/IMenuProvider.cs` - Interface for menu provider abstraction
- **FILE-009**: `projects/Aura/src/Decoration/MenuProvider.cs` - Closure-based menu provider implementation
- **FILE-010**: `projects/Aura/src/Decoration/ScopedMenuProvider.cs` - DI-based menu provider implementation
- **FILE-011**: `projects/Aura/src/Decoration/WindowDecorationBuilder.cs` - Fluent builder with preset factory methods
- **FILE-012**: `projects/Aura/src/Decoration/Serialization/MenuOptionsJsonConverter.cs` - Custom JSON converter for MenuOptions
- **FILE-013**: `projects/Aura/src/Decoration/Serialization/WindowDecorationJsonContext.cs` - Source generator context
- **FILE-014**: `projects/Aura/src/Decoration/WindowDecorationSettings.cs` - Persistent settings class
- **FILE-015**: `projects/Aura/src/Decoration/WindowDecorationSettingsService.cs` - Settings service wrapper
- **FILE-016**: `projects/Aura/src/Decoration/WindowBackdropService.cs` - Backdrop coordinator service
- **FILE-017**: `projects/Aura/src/Decoration/ServiceCollectionExtensions.cs` - DI registration extensions
- **FILE-018**: `projects/Aura/src/WindowManagement/WindowContext.cs` - Updated with Decoration property
- **FILE-019**: `projects/Aura/src/WindowManagement/WindowManagerService.cs` - Updated with decoration resolution
- **FILE-020**: `projects/Aura/src/Views/MainShellView.xaml` - Updated with decoration bindings
- **FILE-021**: `projects/Aura/src/Converters/ButtonPlacementToAlignmentConverter.cs` - XAML value converter
- **FILE-022**: `projects/Aura/tests/Decoration/WindowDecorationOptionsTests.cs` - Unit tests for options and validation
- **FILE-023**: `projects/Aura/tests/Decoration/MenuProviderTests.cs` - Unit tests for menu providers
- **FILE-024**: `projects/Aura/tests/Decoration/WindowDecorationBuilderTests.cs` - Unit tests for builder
- **FILE-025**: `projects/Aura/tests/Decoration/SerializationTests.cs` - Unit tests for JSON serialization
- **FILE-026**: `projects/Aura/tests/Decoration/WindowDecorationSettingsTests.cs` - Unit tests for settings
- **FILE-027**: `projects/Aura/tests/Decoration/WindowBackdropServiceTests.cs` - Unit tests for backdrop service
- **FILE-028**: `projects/Aura/tests/WindowManagement/WindowContextTests.cs` - Integration tests for WindowContext
- **FILE-029**: `projects/Aura/tests/WindowManagement/WindowManagerServiceDecorationTests.cs` - Integration tests
- **FILE-030**: `projects/Aura/tests/Decoration/ServiceCollectionExtensionsTests.cs` - Integration tests for DI
- **FILE-031**: `projects/Aura/tests/Views/MainShellViewTests.cs` - UI tests for XAML bindings
- **FILE-032**: `projects/Aura/docs/WindowDecoration.md` - Developer documentation
- **FILE-033**: `projects/Aura/docs/Migration-WindowDecoration.md` - Migration guide
- **FILE-034**: `tooling/samples/WindowDecorationSample/` - Sample application directory

## 6. Testing

- **TEST-001**: Unit test WindowDecorationOptions.Validate() with all validation rules (AC-013, AC-014, AC-015)
- **TEST-002**: Unit test all WindowDecorationBuilder preset factory methods produce correct defaults (AC-001, AC-002, AC-003)
- **TEST-003**: Unit test WindowDecorationBuilder fluent customization and method chaining (AC-004, AC-006)
- **TEST-004**: Unit test Build() validation throws ArgumentException on invalid options (AC-005)
- **TEST-005**: Unit test MenuProvider thread-safe CreateMenuSource() with concurrent calls (AC-022)
- **TEST-006**: Unit test ScopedMenuProvider DI resolution and menu source creation
- **TEST-007**: Unit test MenuOptionsJsonConverter serialization/deserialization (AC-010)
- **TEST-008**: Unit test WindowDecorationSettings round-trip persistence (AC-010, AC-011)
- **TEST-009**: Unit test WindowBackdropService backdrop application with theme coordination (AC-016, AC-017, AC-018)
- **TEST-010**: Integration test WindowContext.Create() with menu provider resolution (AC-007, AC-008)
- **TEST-011**: Integration test WindowContext.Dispose() releases menu source (AC-020)
- **TEST-012**: Integration test WindowManagerService decoration resolution (explicit, registry, inferred, fallback) (AC-019)
- **TEST-013**: Integration test concurrent window creation without race conditions (AC-021)
- **TEST-014**: Integration test ServiceCollectionExtensions DI registration
- **TEST-015**: Integration test settings override precedence (AC-012)
- **TEST-016**: UI test MainShellView XAML bindings render correctly with different presets
- **TEST-017**: UI test title bar height, padding, icon, and title visibility
- **TEST-018**: UI test window button visibility and placement
- **TEST-019**: UI test menu rendering with compact and standard modes
- **TEST-020**: UI test drag region behavior (default, extended, custom, none)
- **TEST-021**: Performance test decoration resolution completes in ≤2ms (PER-001)
- **TEST-022**: Performance test menu provider resolution is O(n) with n < 10 (PER-002)
- **TEST-023**: Load test concurrent window creation with 100+ windows validates thread safety

## 7. Risks & Assumptions

### Risks

- **RISK-001**: WinUI backdrop APIs may have platform-specific limitations or bugs - Mitigated by graceful degradation and comprehensive error handling (AC-017, AC-018)
- **RISK-002**: Performance of DI resolution for menu providers in high-frequency window creation scenarios - Mitigated by singleton provider registration and lazy menu source creation
- **RISK-003**: Breaking changes in IMenuSource contract from Menus module - Mitigated by strong dependency versioning and integration tests
- **RISK-004**: System.Text.Json serialization issues with complex nested records - Mitigated by custom converters and source generators
- **RISK-005**: Thread-safety issues in MenuBuilder if not designed for concurrent use - Mitigated by locking in MenuProvider.CreateMenuSource()
- **RISK-006**: Users may expect runtime decoration mutation which is not supported - Mitigated by clear documentation (CON-007, GUD-004)
- **RISK-007**: Migration complexity for existing Aura applications - Mitigated by maintaining backward compatibility (CON-002) and providing migration guide

### Assumptions

- **ASSUMPTION-001**: The Menus module's IMenuSource is designed for per-window instances and does not share mutable state
- **ASSUMPTION-002**: AppearanceSettingsService provides synchronous theme mode access without async overhead
- **ASSUMPTION-003**: `ISettingsService<T>` handles file I/O asynchronously and supports concurrent access
- **ASSUMPTION-004**: WinUI Window.SetBackdrop APIs are stable and performant in production scenarios
- **ASSUMPTION-005**: MenuBuilder factory functions execute in <1ms for typical menu hierarchies
- **ASSUMPTION-006**: Application developers will use DI container for menu provider registration, not manual instantiation
- **ASSUMPTION-007**: Window lifetime is long enough that menu source disposal on WindowContext.Dispose() is acceptable
- **ASSUMPTION-008**: Primary window close button requirement (CON-010) is universally applicable and not overridable

## 8. Related Specifications / Further Reading

- [Design Specification - Aura Window Decoration System](file:///f:/projects/DroidNet/plan/window-decorations-spec.md) - Complete technical specification for the decoration system
- [Refactor Aura Window Management Phase 1](file:///f:/projects/DroidNet/plan/refactor-aura-window-management-1.md) - Foundation for window management architecture
- [WinUI 3 Window Customization Documentation](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/windowing/windowing-overview) - Platform backdrop APIs and title bar customization
- [System.Text.Json Source Generators](https://learn.microsoft.com/en-us/dotnet/standard/serialization/system-text-json/source-generation) - Performance optimization for JSON serialization
- [Microsoft.Extensions.DependencyInjection](https://learn.microsoft.com/en-us/dotnet/core/extensions/dependency-injection) - Dependency injection patterns and best practices
- [Immutable Collections in C#](https://learn.microsoft.com/en-us/dotnet/api/system.collections.immutable) - Thread-safe immutable data structures
- [Builder Pattern](https://refactoring.guru/design-patterns/builder) - Fluent API design pattern
- [Provider Pattern](https://en.wikipedia.org/wiki/Provider_model) - Factory abstraction for creating window-scoped dependencies
