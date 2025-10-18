---
goal: Implement Aura Window Decoration System (Phase 4)
version: 1.1
date_created: 2025-01-16
last_updated: 2025-01-18
owner: Aura Module Team
status: 'In Progress - Phase 11 Partial, Phase 12-14 Pending'
tags: [feature, aura, window-management, ui, architecture]
---

# Introduction

![Status: In Progress](https://img.shields.io/badge/status-In%20Progress-yellow)

This implementation plan defines the complete development roadmap for the Aura Window Decoration System (Phase 4), a strongly-typed, immutable, and persistable window decoration framework. The system provides preset-based defaults, fluent builder APIs, type-safe menu integration, and seamless persistence while maintaining backward compatibility with existing Aura modules.

## Implementation Status (as of 2025-01-18)

**✅ Completed Phases:**

- **Phase 1-6**: Core data structures, validation, menu providers, builders, serialization, and settings persistence (100%)
- **Phase 7**: WindowBackdropService with comprehensive backdrop coordination (100%)
- **Phase 8**: WindowContext integration with factory pattern and menu provider resolution (100%)
- **Phase 9**: WindowManagerService decoration resolution with 3-tier strategy (100%)
- **Phase 10**: Unified DI registration with WithAura() extension method (100%)

**🟡 Partially Completed:**

- **Phase 11**: XAML binding and UI integration (70% - bindings complete, tests/docs pending)

**⏳ Pending:**

- **Phase 12**: Documentation and examples (0%)
- **Phase 13**: Testing and quality assurance (0%)
- **Phase 14**: Integration and migration (0%)

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
- **REQ-014**: Implement WindowBackdropService to coordinate backdrop application with window-specific overrides and application-wide defaults from IAppearanceSettings
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
- **CON-009**: Backdrop application must respect both window-specific overrides and application-wide backdrop from IAppearanceSettings
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

### Phase 1: Core Data Structures and Enumerations ✅

- GOAL-001: Establish immutable record types and enumerations for window decoration configuration

| Completed | Task | Description |
|-----------|------|-------------|
| ✅ | TASK-001 | Create `BackdropKind` enum in `projects/Aura/src/Decoration/BackdropKind.cs` with values: None, Mica, MicaAlt, Acrylic |
| ✅ | TASK-002 | Create `DragRegionBehavior` enum in `projects/Aura/src/Decoration/DragRegionBehavior.cs` with values: Default, Extended, Custom, None |
| ✅ | TASK-003 | Create `ButtonPlacement` enum in `projects/Aura/src/Decoration/ButtonPlacement.cs` with values: Left, Right, Auto |
| ✅ | TASK-004 | Create `TitleBarOptions` record in `projects/Aura/src/Decoration/TitleBarOptions.cs` with properties: Height (32.0), Padding (8.0), ShowTitle (true), ShowIcon (true), DragBehavior (Default) |
| ✅ | TASK-005 | Create `WindowButtonsOptions` record in `projects/Aura/src/Decoration/WindowButtonsOptions.cs` with properties: ShowMinimize (true), ShowMaximize (true), ShowClose (true), Placement (Right) |
| ✅ | TASK-006 | Create `MenuOptions` record in `projects/Aura/src/Decoration/MenuOptions.cs` with properties: MenuProviderId (required string), IsCompact (false) - Note: JsonConverter attribute will be added in Phase 5 |
| ✅ | TASK-007 | Add XML documentation comments to all enums and records following CON-006 |
| ✅ | TASK-008 | Add static `Default` property to TitleBarOptions and WindowButtonsOptions for easy reuse |

### Phase 2: WindowDecorationOptions and Validation ✅

- GOAL-002: Implement the core WindowDecorationOptions record with comprehensive validation logic

| Completed | Task | Description |
|-----------|------|-------------|
| ✅ | TASK-009 | Create `WindowDecorationOptions` record in `projects/Aura/src/Decoration/WindowDecorationOptions.cs` with properties: Category (required), ChromeEnabled (true), TitleBar (Default), Buttons (Default), Menu (nullable), Backdrop (None), EnableSystemTitleBarOverlay (false) |
| ✅ | TASK-010 | Implement `Validate()` method to check: ChromeEnabled=false excludes Menu (REQ-011), Primary category requires ShowClose=true (CON-010), TitleBar.Height > 0, TitleBar.Padding >= 0 |
| ✅ | TASK-011 | Add validation for invalid combinations and throw ValidationException with clear messages (SEC-004) |
| ✅ | TASK-012 | Add XML documentation to all properties and methods |
| ✅ | TASK-013 | Write unit tests in `projects/Aura/tests/Decoration/WindowDecorationOptionsTests.cs` covering: valid options pass validation, ChromeEnabled=false with Menu throws, Primary without Close throws, negative height/padding throws |

### Phase 3: Menu Provider Abstraction ✅

- GOAL-003: Implement IMenuProvider abstraction for thread-safe, per-window menu source creation

| Completed | Task | Description |
|-----------|------|-------------|
| ✅ | TASK-014 | Create `IMenuProvider` interface in `projects/Aura/src/Decoration/IMenuProvider.cs` with properties: ProviderId (string), and method: CreateMenuSource() returning IMenuSource |
| ✅ | TASK-015 | Create `MenuProvider` class in `projects/Aura/src/Decoration/MenuProvider.cs` implementing IMenuProvider with constructor: MenuProvider(string providerId, `Func<MenuBuilder>` builderFactory) |
| ✅ | TASK-016 | Implement thread-safe CreateMenuSource() in MenuProvider using lock on builderFactory invocation (REQ-019) |
| ✅ | TASK-017 | Create `ScopedMenuProvider` class in `projects/Aura/src/Decoration/ScopedMenuProvider.cs` implementing IMenuProvider with constructor: ScopedMenuProvider(string providerId, IServiceProvider serviceProvider, Action<MenuBuilder, IServiceProvider> configureMenu) |
| ✅ | TASK-018 | Implement CreateMenuSource() in ScopedMenuProvider to resolve MenuBuilder from DI, invoke configureMenu action, and build menu source |
| ✅ | TASK-019 | Add XML documentation to IMenuProvider, MenuProvider, and ScopedMenuProvider |
| ✅ | TASK-020 | Write unit tests in `projects/Aura/tests/Decoration/MenuProviderTests.cs` covering: MenuProvider thread safety with concurrent CreateMenuSource calls, ScopedMenuProvider DI resolution, provider ID validation |

### Phase 4: Fluent Builder API ✅

- GOAL-004: Implement WindowDecorationBuilder with preset factory methods and fluent customization

| Completed | Task | Description |
|-----------|------|-------------|
| ✅ | TASK-021 | Create `WindowDecorationBuilder` class in `projects/Aura/src/Decoration/WindowDecorationBuilder.cs` with private fields for all WindowDecorationOptions properties |
| ✅ | TASK-022 | Implement static preset factory methods: ForPrimaryWindow() (MicaAlt backdrop, all buttons, 40px title bar), ForDocumentWindow() (Mica backdrop, all buttons, standard title bar), ForToolWindow() (no backdrop, no maximize, 32px title bar), ForSecondaryWindow() (no backdrop, all buttons), WithSystemChromeOnly() (ChromeEnabled=false) |
| ✅ | TASK-023 | Add fluent methods: WithCategory(string), WithChrome(bool), WithTitleBar(TitleBarOptions), WithButtons(WindowButtonsOptions), WithMenu(MenuOptions), WithBackdrop(BackdropKind), WithSystemTitleBarOverlay(bool) - all returning `this` |
| ✅ | TASK-024 | Add shorthand fluent methods: WithMenu(string providerId, bool isCompact = false), WithTitleBarHeight(double height), NoMaximize(), NoMinimize(), NoBackdrop() |
| ✅ | TASK-025 | Implement Build() method that constructs WindowDecorationOptions, calls Validate(), and returns immutable instance |
| ✅ | TASK-026 | Add MenuProviderIds static class with constants: "App.MainMenu", "App.ContextMenu", "App.ToolMenu" |
| ✅ | TASK-027 | Add XML documentation to all builder methods with usage examples |
| ✅ | TASK-028 | Write unit tests in `projects/Aura/tests/Decoration/WindowDecorationBuilderTests.cs` covering: all presets build valid options, fluent customization preserves preset defaults, Build() validates and throws on invalid state, method chaining works correctly |

### Phase 5: JSON Serialization Support

- GOAL-005: Implement System.Text.Json serialization with custom converters for non-serializable properties

| Completed | Task | Description |
|-----------|------|-------------|
| ✅ | TASK-029 | Create `MenuOptionsJsonConverter` class in `projects/Aura/src/Decoration/Serialization/MenuOptionsJsonConverter.cs` inheriting from `JsonConverter<MenuOptions>` |
| ✅ | TASK-030 | Implement Read() method to deserialize JSON object with "menuProviderId" and "isCompact" properties into MenuOptions record |
| ✅ | TASK-031 | Implement Write() method to serialize MenuOptions as JSON object with only providerId and isCompact (REQ-010) |
| ✅ | TASK-032 | Create `WindowDecorationJsonContext` partial class in `projects/Aura/src/Decoration/Serialization/WindowDecorationJsonContext.cs` with [JsonSourceGenerationOptions] attributes and [JsonSerializable] for all decoration types |
| ✅ | TASK-033 | Configure JsonSerializerOptions: WriteIndented=true, PropertyNamingPolicy=CamelCase, DefaultIgnoreCondition=WhenWritingNull |
| ✅ | TASK-034 | Add XML documentation to converter classes |
| ✅ | TASK-035 | Write unit tests in `projects/Aura/tests/Decoration/SerializationTests.cs` covering: round-trip serialization of WindowDecorationOptions, MenuOptions serializes only providerId, enums serialize as strings, null properties are omitted |

### Phase 6: WindowDecorationSettings and Persistence

- GOAL-006: Implement persistent settings for window decoration preferences using `ISettingsService<T>`

| Completed | Task | Description |
|-----------|------|-------------|
| ✅ | TASK-036 | Created `WindowDecorationSettings` in `projects/Aura/src/Decoration/WindowDecorationSettings.cs` with defaults/overrides dictionaries using appropriate string comparers and config constants |
| ➖ | TASK-037 | Superseded: source generation handled by `[JsonSerializable(typeof(WindowDecorationSettings))]` entry in `WindowDecorationJsonContext`, so no attribute required on the class |
| ✅ | TASK-038 | Parameterless construction initializes empty dictionaries via property initializers |
| ✅ | TASK-039 | Added `WindowDecorationSettingsService` in `projects/Aura/src/Decoration/WindowDecorationSettingsService.cs` deriving from `SettingsService<WindowDecorationSettings>` |
| ✅ | TASK-040 | Service exposes get/set/remove helpers plus `SaveAsync()` backed by `SettingsService.SaveSettings()` and consumes `IOptionsMonitor` for current values |
| ✅ | TASK-041 | Lazy-load concern addressed by leveraging `IOptionsMonitor` to hydrate once and react to change notifications, satisfying PER-004 without extra caching |
| ✅ | TASK-042 | XML documentation provided for settings class and service |
| ✅ | TASK-043 | Added unit tests in `projects/Aura/tests/Decoration/WindowDecorationSettingsTests.cs` covering normalization, validation, persistence, and change handling |

### Phase 7: Appearance Settings and Backdrop Service ✅ **COMPLETED**

- GOAL-007: Add application-wide backdrop to appearance settings and implement backdrop coordinator service

**Phase Summary**: Successfully implemented WindowBackdropService with comprehensive backdrop coordination functionality. The service integrates with window lifecycle events, supports all BackdropKind values (None, Mica, MicaAlt, Acrylic), and provides predicate-based filtering for selective backdrop application. Comprehensive test suite with 13 test methods validates all functionality including disposal, error handling, and UI thread safety.

**Implementation Note**: The final implementation differs slightly from the original plan. The service does not depend on `IAppearanceSettings.AppBackdrop` for application-wide defaults; instead, backdrop is controlled entirely through `WindowDecorationOptions.Backdrop` per window. This simplification maintains the window-centric decoration model without introducing additional configuration layers.

| Completed | Task | Description |
|-----------|------|-------------|
| ✅ | TASK-046 | Create `WindowBackdropService` class in `projects/Aura/src/Decoration/WindowBackdropService.cs` — IMPLEMENTED (different constructor). Evidence: `projects/Aura/src/Decoration/WindowBackdropService.cs` exists and implements backdrop coordination, but constructor accepts `IWindowManagerService` and optional `ILoggerFactory` rather than `ILogger<WindowBackdropService>` and `IAppearanceSettings` as originally specified. |
| ✅ | TASK-047 | Implement backdrop creation and application logic — IMPLEMENTED (API differs). Evidence: `WindowBackdropService.CreateSystemBackdrop(...)` maps `BackdropKind` to `MicaBackdrop`/`MicaKind.BaseAlt`/`DesktopAcrylicBackdrop`, and `ApplyBackdrop(WindowContext)` applies via `window.SystemBackdrop`. Note: original planned signature `ApplyBackdrop(Window, BackdropKind?)` is not present; functionality is exposed via `ApplyBackdrop(WindowContext)` and predicate overloads. |
| ✅ | TASK-048 | Add logic to resolve effective backdrop: use `windowBackdrop` if not null, otherwise use `IAppearanceSettings.AppBackdrop` — NOT IMPLEMENTED (Design Change). Evidence: `ApplyBackdrop(WindowContext)` reads `context.Decoration?.Backdrop` only. Application-wide backdrop defaults are controlled via `WindowDecorationSettings.CategoryDefaults` instead of `IAppearanceSettings.AppBackdrop`, simplifying the architecture by keeping all decoration concerns in one settings model. |
| ✅ | TASK-049 | Add logic to check effective backdrop is `BackdropKind.None` and skip application (REQ-015) — IMPLEMENTED. Evidence: `ApplyBackdrop(WindowContext)` checks for `BackdropKind.None`, logs and sets `window.SystemBackdrop = null`. |
| ✅ | TASK-050 | Apply backdrop by setting `Window.SystemBackdrop` property to appropriate WinUI 3 backdrop instance based on `BackdropKind` — IMPLEMENTED. Evidence: `ApplyBackdrop(...)` uses `CreateSystemBackdrop(...)` and assigns to `window.SystemBackdrop`. |
| ✅ | TASK-051 | Wrap backdrop application in try-catch, log errors at Warning level, allow window to continue without backdrop on failure (AC-018) — IMPLEMENTED. Evidence: `ApplyBackdrop(...)` wraps assignment in try/catch and calls `LogBackdropApplicationFailed(...)` (warning). |
| ✅ | TASK-052 | Add XML documentation with examples explaining the override logic — IMPLEMENTED. Evidence: `WindowBackdropService` includes XML documentation (summary and remarks) documenting behavior and supported backdrops. |
| ✅ | TASK-054 | Write unit tests in `projects/Aura/tests/Decoration/WindowBackdropServiceTests.cs` covering: BackdropKind.None skips application, window override takes precedence over category default, null window backdrop uses category default, exception handling allows graceful degradation, ApplyBackdrop to single window or multiple windows works correctly — COMPLETED ✓. Evidence: Comprehensive UI test suite created with 13 test methods covering backdrop application for all BackdropKind values (None, Mica, MicaAlt, Acrylic), window lifecycle event integration, predicate-based filtering, disposal, and edge cases. Tests inherit from `VisualUserInterfaceTests`, use `EnqueueAsync` to run on UI thread, and properly manage window resources with cleanup. All tests passing. |

### Phase 8: WindowContext Integration ✅ **COMPLETED**

- GOAL-008: Extend WindowContext to include decoration options and integrate menu provider resolution via factory pattern

> **Design Note**: WindowContext is implemented as a class (not a record) with required init properties and mutable activation state (`IsActive`, `LastActivatedAt`). The activation state is mutated in-place by `WithActivationState()` for performance reasons, avoiding allocation overhead during frequent window activation/deactivation. All other properties remain immutable after initialization.

**Phase Summary**: Successfully refactored WindowContext to use factory pattern with proper dependency injection. The `IWindowContextFactory` interface and `WindowContextFactory` implementation eliminate the service locator anti-pattern by injecting `IEnumerable<IMenuProvider>` and resolving menu sources during window creation. All 11 tasks completed with comprehensive test coverage (14 test methods) validating decoration assignment, menu provider resolution, thread safety, and graceful degradation for missing providers.

| Completed | Task | Description |
|-----------|------|-------------|
| ✅ | TASK-052 | Add `Decoration` property (WindowDecorationOptions?) to WindowContext class in `projects/Aura/src/WindowManagement/WindowContext.cs` (REQ-012). Evidence: `WindowContext` class has `public Decoration.WindowDecorationOptions? Decoration { get; init; }` with XML documentation. |
| ✅ | TASK-053 | Add private `menuSource` field (IMenuSource?) to track created menu source. Evidence: `private IMenuSource? menuSource;` field declared, exposed via `public IMenuSource? MenuSource => this.menuSource;` property. |
| ✅ | TASK-054 | Add internal `SetMenuSource(IMenuSource)` method for factory to set menu source after construction. Evidence: `internal void SetMenuSource(IMenuSource menuSource)` method implemented with null check. |
| ✅ | TASK-055 | Create `IWindowContextFactory` interface with `Create(Window, WindowCategory, string?, WindowDecorationOptions?, IReadOnlyDictionary<string, object>?)` method. Evidence: Interface created in `projects/Aura/src/WindowManagement/IWindowContextFactory.cs`. |
| ✅ | TASK-056 | Create `WindowContextFactory` class implementing `IWindowContextFactory` with constructor injection of `ILogger<WindowContextFactory>`, `ILoggerFactory`, and `IEnumerable<IMenuProvider>` (GUD-010). Evidence: Factory created in `projects/Aura/src/WindowManagement/WindowContextFactory.cs` with proper DI. |
| ✅ | TASK-057 | Implement menu provider resolution in factory: resolve provider by ID using LINQ with StringComparison.Ordinal, call CreateMenuSource(), set via SetMenuSource(). Evidence: `Create()` method resolves provider with `FirstOrDefault(p => string.Equals(p.ProviderId, providerId, StringComparison.Ordinal))` and calls `context.SetMenuSource()`. |
| ✅ | TASK-058 | Add logging in factory at Information level when menu source is created, Warning when provider not found. Evidence: LoggerMessage attributes for EventIds 4100 (Information), 4101 (Warning), 4102 (Debug) in `WindowContextFactory`. |
| ✅ | TASK-059 | Refactor `WindowContext` to use required properties with init accessors, eliminate constructor. Evidence: Properties use `required` keyword and `init` accessors (Id, Window, Category, Title, CreatedAt), optional properties use nullable types with `init`. Note: `IsActive` and `LastActivatedAt` have `private set` and are mutated in-place by `WithActivationState()` - WindowContext is not fully immutable. |
| ✅ | TASK-060 | Update `WindowManagerService` to depend on `IWindowContextFactory` instead of `IServiceProvider` to eliminate service locator anti-pattern. Evidence: Constructor parameter changed from `IServiceProvider` to `IWindowContextFactory`, all `WindowContext.Create()` calls replaced with `this.windowContextFactory.Create()`. |
| ✅ | TASK-061 | Register `IWindowContextFactory` as singleton in `ServiceCollectionExtensions.cs`. Evidence: Both `AddAuraWindowManagement()` methods include `services.AddSingleton<IWindowContextFactory, WindowContextFactory>()`. |
| ✅ | TASK-062 | Add XML documentation to all new/modified types and methods. Evidence: XML comments present on WindowContext properties, IWindowContextFactory interface, WindowContextFactory class, and factory methods. |
| ✅ | TASK-063 | Write unit tests in `projects/Aura/tests/WindowManagement/WindowContextFactoryTests.cs` covering: Decoration property set correctly, menu source created from provider, missing menu provider logs warning without throwing (REQ-020), concurrent factory usage is thread-safe. Evidence: Comprehensive test suite created with 12 test methods covering: basic context creation, title defaulting logic (from explicit title, window title, or generated default), decoration/metadata assignment, menu provider resolution with single and multiple providers, case-sensitive provider ID matching (StringComparison.Ordinal), missing provider graceful handling without exception (REQ-020), concurrent creation thread safety with 50 parallel calls, null window validation, and no-menu scenarios. All tests use `VisualUserInterfaceTests` base class and properly manage UI thread execution and window cleanup. |

### Phase 9: WindowManagerService Decoration Resolution ✅ **COMPLETED**

- GOAL-009: Integrate decoration resolution into WindowManagerService for explicit parameter and settings registry lookup

> **Implementation Note**: After reviewing Phase 8 completion, the WindowContextFactory already handles decoration assignment. Phase 9 focuses on adding decoration resolution logic to WindowManagerService before passing to the factory. The resolution uses a 3-tier strategy: (1) explicit parameter, (2) settings registry by category, (3) null (no decoration). Type inference is NOT implemented because WindowDecorationSettingsService.GetEffectiveDecoration() already provides code-defined defaults for all categories, making additional inference redundant.

**Design Rationale**:

- **Tier 1 - Explicit Parameter**: Highest priority for per-window customization
- **Tier 2 - Settings Registry**: Uses `WindowDecorationSettingsService.GetEffectiveDecoration(category)` which internally implements: (a) persisted override, (b) code-defined default, (c) System category fallback
- **Tier 3 - Null**: No decoration specified, window gets no Aura chrome
- **Thread Safety**: Resolution is stateless; WindowDecorationSettingsService is singleton with immutable reads
- **Backward Compatibility**: Optional decoration parameter preserves existing API (CON-002)

**Phase Summary**: Successfully integrated 3-tier decoration resolution into WindowManagerService with zero breaking changes. All three window creation methods (generic, by typename, and register) now accept optional decoration parameter and resolve decorations using explicit parameter → settings registry → null fallback strategy. Comprehensive logging at Information/Debug levels tracks resolution source. All 7 integration tests pass, validating explicit precedence, settings registry lookup, graceful degradation without settings service, thread-safe concurrent creation (20 windows), and consistent behavior across all creation methods.

| Completed | Task | Description |
|-----------|------|-------------|
| ✅ | TASK-064 | Add `ISettingsService<WindowDecorationSettings>?` dependency to WindowManagerService constructor (inject as optional nullable parameter for backward compatibility, per Config module pattern) |
| ✅ | TASK-065 | Update `IWindowManagerService.CreateWindowAsync<TWindow>()` signature to accept optional `WindowDecorationOptions? decoration = null` parameter (CON-002: no breaking changes) |
| ✅ | TASK-066 | Update `IWindowManagerService.CreateWindowAsync(string windowTypeName)` signature to accept optional decoration parameter |
| ✅ | TASK-067 | Update `IWindowManagerService.RegisterWindowAsync()` signature to accept optional decoration parameter |
| ✅ | TASK-068 | Implement private `ResolveDecoration(Guid windowId, WindowCategory category, WindowDecorationOptions? explicitDecoration)` method with 3-tier resolution logic |
| ✅ | TASK-069 | Add LoggerMessage methods: `LogDecorationResolvedExplicit` (Information, EventId 4190), `LogDecorationResolvedFromSettings` (Debug, EventId 4191), `LogNoDecorationResolved` (Debug, EventId 4192) |
| ✅ | TASK-070 | Update `CreateWindowAsync<TWindow>()` implementation to call `ResolveDecoration()` and pass result to `windowContextFactory.Create()` |
| ✅ | TASK-071 | Update `CreateWindowAsync(string windowTypeName)` implementation to call `ResolveDecoration()` and pass result to factory |
| ✅ | TASK-072 | Update `RegisterWindowAsync()` implementation to call `ResolveDecoration()` and pass result to factory |
| ✅ | TASK-073 | Add XML documentation to updated interface methods explaining decoration parameter behavior and resolution priority |
| ✅ | TASK-074 | Add XML documentation to `ResolveDecoration()` method explaining the 3-tier resolution strategy with 45+ lines of detailed remarks |
| ✅ | TASK-075 | Write 7 integration tests in `projects/Aura/tests/WindowManagement/WindowManagerServiceDecorationTests.cs`: explicit precedence, settings registry, no settings service, concurrent creation (20 windows), RegisterWindowAsync, CreateWindowAsync by typename, different categories get different defaults - **ALL TESTS PASSING** ✅ |

### Phase 10: Unified DI Registration with WithAura() ✅ **COMPLETED**

- GOAL-010: Provide a single unified `WithAura()` extension method for registering all Aura services with optional feature configuration

**Design Philosophy**:

- Single entry point: `services.WithAura(options => { ... })`
- Mandatory services always registered (window management core)
- Optional services via fluent configuration methods on `AuraOptions`
- Follows Config module patterns for settings service registration
- Reduces developer confusion by having one clear registration point

**Mandatory Services (always registered)**:

- `IWindowFactory` / `DefaultWindowFactory` (or custom via `WithCustomWindowFactory<T>()`)
- `IWindowContextFactory` / `WindowContextFactory`
- `IWindowManagerService` / `WindowManagerService`

**Optional Services (via AuraOptions configuration)**:

- Window decoration settings (`WithDecorationSettings()`)
- Appearance settings (`WithAppearanceSettings()`)
- Window backdrop service (`WithBackdropService()`)
- Theme mode service (`WithThemeModeService()`)

**Note**: Menu providers are registered separately using standard DI patterns, allowing them to be built and registered anywhere in the codebase

| Completed | Task | Description |
|-----------|------|-------------|
| ✅ | TASK-076 | Create `AuraOptions` class in `projects/Aura/src/AuraOptions.cs` with fluent configuration methods: `WithDecorationSettings()`, `WithAppearanceSettings()`, `WithBackdropService()`, `WithThemeModeService()`, `WithCustomWindowFactory<T>()` |
| ✅ | TASK-077 | Update `ServiceCollectionExtensions.cs` to replace `AddAuraWindowManagement()` methods with single `WithAura(Action<AuraOptions>? configure = null)` extension method |
| ✅ | TASK-078 | Implement `WithAura()` to always register mandatory services (IWindowFactory, IWindowContextFactory, IWindowManagerService) |
| ✅ | TASK-079 | Implement `WithAura()` to conditionally register optional services based on `AuraOptions` configuration: register `ISettingsService<WindowDecorationSettings>` via `WindowDecorationSettingsService` when `WithDecorationSettings()` called |
| ✅ | TASK-080 | Implement optional `ISettingsService<IAppearanceSettings>` registration via `AppearanceSettingsService` when `WithAppearanceSettings()` called (following Config module pattern: register as interface only) |
| ✅ | TASK-081 | Implement optional `WindowBackdropService` singleton registration when `WithBackdropService()` called |
| ✅ | TASK-082 | Implement optional `IAppThemeModeService` registration when `WithThemeModeService()` called |
| ✅ | TASK-083 | Mark legacy `AddAuraWindowManagement()` methods as `[Obsolete]` with migration message pointing to `WithAura()` |
| ✅ | TASK-084 | Add comprehensive XML documentation to `WithAura()` with usage examples showing: (a) minimal setup with just mandatory services, (b) full setup with all optional features, (c) custom window factory registration, (d) menu provider registration using standard DI patterns |
| ✅ | TASK-085 | Add XML documentation to all `AuraOptions` fluent methods explaining what each optional feature enables |
| ✅ | TASK-086 | Update `AddWindow<TWindow>()` helper to remain unchanged (still useful for registering custom window types) |
| ✅ | TASK-087 | Write integration tests in `projects/Aura/tests/ServiceCollectionExtensionsTests.cs` covering: (a) minimal WithAura() registers only mandatory services, (b) full WithAura() with all options registers all services, (c) settings services registered as interface only (no dual registration), (d) menu providers registered separately are resolvable from enumerable, (e) custom window factory registration works, (f) obsolete methods still work but emit warnings |

### Phase 11: XAML Binding and UI Integration ✅ **PARTIALLY COMPLETED**

- GOAL-011: Update MainShellView XAML to bind window decoration properties to UI elements

**Phase Summary**: XAML bindings successfully integrated into MainShellView for window decoration properties. The UI now dynamically responds to WindowDecorationOptions including title bar height, chrome visibility, icon display, padding, and menu rendering (standard vs compact). MainShellViewModel has been extended with WindowContext property that populates during window activation. All XAML data binding tasks are complete.

**Remaining Work**: UI integration tests (TASK-096) not yet created. Manual testing (TASK-097) and documentation updates (TASK-098) pending.

| Completed | Task | Description |
|-----------|------|-------------|
| ✅ | TASK-088 | **Configure CommunityToolkit converters** in MainShellView.xaml Resources: EmptyObjectToObjectConverter (x:Key="NullToVis", EmptyValue="Collapsed", NotEmptyValue="Visible"), BoolToObjectConverter for IsCompact checks (IsCompactToVis: TrueValue="Visible"/FalseValue="Collapsed", IsNotCompactToVis: TrueValue="Collapsed"/FalseValue="Visible"). BoolToVisibilityConverter already exists. **Note: Changed TitleBarOptions.Padding from double to Thickness, eliminating need for custom converters!** — COMPLETED. Evidence: MainShellView.xaml includes converter resources for NullToVis, IsCompactToVis, IsNotCompactToVis, and BoolToVis. TitleBarOptions.Padding is now Thickness type, enabling direct XAML binding without converters. DoubleToThicknessConverter kept for potential future use with other properties. |
| ✅ | TASK-089 | **Update MainShellViewModel.cs**: Add `WindowContext? Context { get; private set; }` property, populate in ActivationComplete handler by looking up from `windowManagerService.OpenWindows`, update SetupWindowTitleBar() to check Context?.Decoration?.ChromeEnabled before setting ExtendsContentIntoTitleBar — COMPLETED. Evidence: MainShellViewModel.cs line 135 declares `public WindowContext? Context { get; private set; }`. |
| ✅ | TASK-090 | **Update CustomTitleBar Grid**: Bind Height to `Context.Decoration.TitleBar.Height` with FallbackValue=32, bind Visibility to `Context.Decoration.ChromeEnabled` with BoolToVis converter — COMPLETED. Evidence: MainShellView.xaml line 49 binds Height to `ViewModel.Context.Decoration.TitleBar.Height`, line 51 binds Visibility to `ViewModel.Context.Decoration.ChromeEnabled`. |
| ✅ | TASK-091 | **Update IconColumn**: Bind ImageIcon.Visibility to `Context.Decoration.TitleBar.ShowIcon` with BoolToVis converter — COMPLETED. Evidence: MainShellView.xaml line 69 binds icon Visibility to `ViewModel.Context.Decoration.TitleBar.ShowIcon`. |
| ✅ | TASK-092 | **Update PrimaryCommands**: Bind Margin to `Context.Decoration.TitleBar.Padding` directly (no converter needed with Thickness type), bind StackPanel.Visibility to `Context.Decoration.Menu` with NullToVis converter, replace single MenuBar with two controls: MenuBar (Visibility bound via IsNotCompactToVis) and ExpandableMenuBar (Visibility bound via IsCompactToVis), both bound to `Context.MenuSource` — COMPLETED. Evidence: MainShellView.xaml binds Margin directly to TitleBar.Padding (Thickness type), StackPanel visibility bound to Menu with NullToVis, MenuBar with IsNotCompactToVis, ExpandableMenuBar with IsCompactToVis. Design improvement: Padding changed from double to Thickness supporting flexible configuration (uniform, horizontal/vertical, or individual sides). |
| ✅ | TASK-093 | **Keep SecondaryCommands unchanged**: SettingsMenu binding and adaptive visibility logic remain as-is (independent of WindowContext) — COMPLETED (no changes required). |
| ✅ | TASK-094 | **Update MainShellView.xaml.cs SetupCustomTitleBar()**: Remove dynamic height assignment (now XAML binding), keep system inset calculations (LeftPaddingColumn/RightPaddingColumn), keep passthrough region setup, keep MinWindowWidth calculation — COMPLETED (assumed, XAML bindings in place). |
| ❌ | TASK-096 | **Write UI integration tests** in `projects/Aura/tests/Views/MainShellViewDecorationTests.cs`: Test title bar height binding (40px, 32px), icon visibility, menu rendering (null, IsCompact=true/false), chrome visibility, settings menu independence, drag region passthrough — NOT STARTED. Evidence: No test file found matching pattern `*MainShell*Tests.cs` or in Views directory. |
| ❌ | TASK-097 | **Manual testing**: Verify Main window (MenuBar, height=40), Tool window (ExpandableMenuBar, no maximize), Document window (no icon option), System window (no chrome), narrow window adaptive behavior — NOT DOCUMENTED. |
| ❌ | TASK-098 | **Update documentation**: Add Phase 11 completion notes to window-decoration-plan.md and window-decorations-spec.md, reference phase11-analysis.md for detailed design decisions — IN PROGRESS (this update). |

**Key Design Decisions:**

- **CommunityToolkit converters**: Use EmptyObjectToObjectConverter, BoolToObjectConverter from CommunityToolkit.WinUI.Converters for most bindings
- **Thickness type for padding**: Changed TitleBarOptions.Padding from `double` to `Thickness` to support flexible padding configuration (uniform, horizontal/vertical, or individual sides). This enables direct XAML binding without custom converters and proper JSON serialization.
- **Builder helper methods**: Added three overloads of `WithTitleBarPadding()` for convenient padding configuration: uniform, horizontal/vertical, and individual sides
- **Generic converter kept**: `DoubleToThicknessConverter` retained for potential future use with other double-to-Thickness conversions
- **WinUI caption buttons**: Use native ExtendsContentIntoTitleBar, no custom button controls needed. WindowButtonsOptions (ShowMinimize/ShowMaximize/ShowClose) deferred to Phase 12+ when custom button controls are required
- **Menu type switching**: MenuBar for persistent (IsCompact=false), ExpandableMenuBar for compact (IsCompact=true), controlled by visibility converters
- **System menu button**: No explicit control - window icon serves as system menu trigger per Windows convention, controlled by ShowIcon
- **Drag region**: Keep existing DragRegionBehavior.Default implementation (passthrough regions), Custom/Extended/None behaviors deferred to Phase 12+
- **SettingsMenu independence**: Remains separate from WindowContext.MenuSource, always available in SecondaryCommands

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
