# Phase 10 Analysis: Unified DI Registration with WithAura()

## Executive Summary

Phase 10 has been **completely redesigned** to provide a single, unified `WithAura()` registration method that:

1. ✅ Eliminates developer confusion by providing ONE clear entry point
2. ✅ Follows Config module best practices for settings service registration
3. ✅ Distinguishes between mandatory and optional features
4. ✅ Prevents common DI anti-patterns (no dual registration, proper interface usage)
5. ✅ Maintains backward compatibility via obsolete legacy methods

## Key Changes from Original Plan

### ❌ Original Plan (Rejected)

- Separate `ServiceCollectionExtensions` class in `Decoration/` folder
- Multiple registration methods: `AddWindowDecorationServices()`, `AddMenuProvider()`, `AddScopedMenuProvider()`
- Required developers to call multiple methods in correct order
- Risk of incorrect registration patterns

### ✅ New Approach (Recommended)

- **Single method**: `services.WithAura(options => { ... })`
- Extend existing `ServiceCollectionExtensions.cs` in Aura root
- Fluent configuration via `AuraOptions` class
- Mandatory services always registered, optional services via explicit opt-in

## Architecture

### Mandatory Services (Always Registered)

These form the core window management infrastructure and **cannot be opted out**:

```csharp
services.WithAura(); // Registers all mandatory services:
// - IWindowFactory / DefaultWindowFactory
// - IWindowContextFactory / WindowContextFactory
// - IWindowManagerService / WindowManagerService
```

### Optional Services (Fluent Configuration)

Developers explicitly opt-in to features they need:

```csharp
services.WithAura(options => options
    .WithDecorationSettings()        // ISettingsService<WindowDecorationSettings>
    .WithAppearanceSettings()        // ISettingsService<IAppearanceSettings>
    .WithBackdropService()           // WindowBackdropService
    .WithThemeModeService()          // IAppThemeModeService
    .WithCustomWindowFactory<MyWindowFactory>()
);

// Menu providers registered separately (can be anywhere in startup code)
services.AddSingleton<IMenuProvider>(new MenuProvider("App.MainMenu", () => ...));
services.AddSingleton<IMenuProvider>(new ScopedMenuProvider("App.ContextMenu", sp, (builder, sp) => ...));
```

## Critical Best Practices Applied

### 1. Settings Service Registration Pattern (from Config README)

**WRONG (Dual Registration - Creates Multiple Instances)**:

```csharp
// ❌ NEVER DO THIS
container.Register<WindowDecorationSettingsService>(Reuse.Singleton);
container.Register<ISettingsService<WindowDecorationSettings>, WindowDecorationSettingsService>(Reuse.Singleton);
```

**CORRECT (Single Interface Registration)**:

```csharp
// ✅ ALWAYS DO THIS
services.AddSingleton<ISettingsService<WindowDecorationSettings>, WindowDecorationSettingsService>();
```

**Consumption**:

```csharp
public class MyService
{
    private readonly ISettingsService<IWindowDecorationSettings> settings;

    public MyService(ISettingsService<IWindowDecorationSettings> settings)
    {
        this.settings = settings;
        var decoration = settings.Settings.GetEffectiveDecoration(category);
    }
}
```

### 2. No Unnecessary Registrations

The original plan included registrations that **already exist**:

- ✅ `IWindowFactory` - already registered in `AddAuraWindowManagement()`
- ✅ `IWindowContextFactory` - already registered
- ✅ `IWindowManagerService` - already registered

**We only add registrations for NEW services**:

- `ISettingsService<WindowDecorationSettings>` (NEW)
- `WindowBackdropService` (NEW)
- `IMenuProvider` implementations (NEW)

### 3. Optional Dependency Pattern

`WindowManagerService` already accepts optional dependencies:

```csharp
public WindowManagerService(
    IWindowFactory windowFactory,
    IWindowContextFactory windowContextFactory,
    HostingContext hostingContext,
    ILoggerFactory loggerFactory,
    IAppThemeModeService? themeModeService = null,                      // Optional
    ISettingsService<IAppearanceSettings>? appearanceSettingsService = null,  // Optional
    IRouter? router = null,                                              // Optional
    ISettingsService<WindowDecorationSettings>? decorationSettingsService = null) // Optional
```

This means:

- ✅ Apps without settings services still work
- ✅ Apps without backdrop service still work
- ✅ Apps without menu providers still work

## Revised Task Breakdown

### Core Tasks

| Task | Description | Rationale |
|------|-------------|-----------|
| TASK-076 | Create `AuraOptions` class | Fluent configuration API for optional features |
| TASK-077 | Replace `AddAuraWindowManagement()` with `WithAura()` | Single unified entry point |
| TASK-078 | Always register mandatory services | Core window management cannot be disabled |
| TASK-079-082 | Conditionally register optional services | Based on `AuraOptions` configuration |

### Migration & Compatibility

| Task | Description | Rationale |
|------|-------------|-----------|
| TASK-083 | Mark legacy methods `[Obsolete]` | Smooth migration path, no breaking changes |
| TASK-084-085 | Comprehensive XML documentation | Clear usage examples for developers |
| TASK-086 | Keep `AddWindow<TWindow>()` unchanged | Still useful for custom window types |
| TASK-087 | Integration tests | Validate correct registration patterns |

## Usage Examples

### Minimal Setup (Mandatory Services Only)

```csharp
services.WithAura();
```

Registers only core window management. Suitable for apps that:

- Don't need persisted decoration settings
- Don't use backdrop effects
- Don't have menus in title bars

### Full Setup (All Optional Features)

```csharp
// Core Aura services with optional features
services.WithAura(options => options
    .WithDecorationSettings()
    .WithAppearanceSettings()
    .WithBackdropService()
    .WithThemeModeService()
);

// Register custom windows
services.AddWindow<MainWindow>();
services.AddWindow<ToolWindow>();

// Menu providers can be registered anywhere (even in different files)
// They are resolved by ID, so order doesn't matter
services.AddSingleton<IMenuProvider>(
    new MenuProvider("App.MainMenu", () => new MenuBuilder()
        .AddItem("File", cmd => fileMenuBuilder)
        .AddItem("Edit", cmd => editMenuBuilder))
);

services.AddSingleton<IMenuProvider>(
    new ScopedMenuProvider("App.ContextMenu", services.BuildServiceProvider(), (builder, sp) =>
    {
        var service = sp.GetRequiredService<IMyService>();
        builder.AddItem("Context Action", cmd => service.Execute());
    })
);
```

### Custom Window Factory

```csharp
services.WithAura(options => options
    .WithCustomWindowFactory<MyCustomWindowFactory>()
    .WithDecorationSettings()
    .WithBackdropService()
);
```

## Testing Strategy

### Unit Tests Required

1. **AuraOptions Fluent API**:
   - Verify all configuration methods return `this` for chaining
   - Verify menu providers are queued correctly
   - Verify custom window factory is stored

2. **WithAura() Registration**:
   - Minimal: only mandatory services registered
   - Full: all optional services registered when configured
   - Settings services registered as interface only (no dual registration)
   - Menu providers resolvable from `IEnumerable<IMenuProvider>`

3. **Backward Compatibility**:
   - Legacy `AddAuraWindowManagement()` still works
   - Obsolete attribute emits warning
   - Migration message points to `WithAura()`

### Integration Tests Required

1. **Settings Service Resolution**:
   - Inject `ISettingsService<WindowDecorationSettings>`
   - Access via `.Settings` property works correctly
   - No duplicate instances created

2. **Menu Provider Resolution**:
   - Multiple menu providers registered
   - All resolvable from `IEnumerable<IMenuProvider>`
   - Thread-safe concurrent resolution

3. **Optional Dependency Injection**:
   - `WindowManagerService` works without optional services
   - `WindowManagerService` uses optional services when registered

## Migration Guide for Developers

### Before (Multiple Methods)

```csharp
services.AddAuraWindowManagement();
services.AddWindowDecorationServices();
services.AddWindow<MainWindow>();
```

### After (Single Method)

```csharp
services.WithAura(options => options
    .WithDecorationSettings()
    .WithBackdropService()
);
services.AddWindow<MainWindow>();

// Menu providers registered separately (can be in different configuration classes)
services.AddSingleton<IMenuProvider>(new MenuProvider("App.MainMenu", () => ...));
services.AddSingleton<IMenuProvider>(new MenuProvider("App.ToolMenu", () => ...));
```

## Benefits Summary

| Benefit | Description |
|---------|-------------|
| **Simplicity** | One method vs. multiple scattered calls |
| **Clarity** | Explicit opt-in for optional features |
| **Safety** | Prevents common DI anti-patterns |
| **Consistency** | Follows Config module established patterns |
| **Backward Compatible** | Obsolete methods still work during migration |
| **Testability** | Clear registration contracts for testing |
| **Discoverability** | Fluent API guides developers through options |

## Menu Provider Registration Design

### Why Menu Providers Are NOT in AuraOptions

Menu providers are intentionally **excluded from `AuraOptions`** for these reasons:

1. **Separation of Concerns**: Menu building logic should be separate from service registration
2. **Flexibility**: Menus can be built in dedicated configuration classes (e.g., `MenuConfiguration.cs`)
3. **Timing Independence**: Menu providers can be registered before or after `WithAura()` is called
4. **ID-Based Coupling**: Windows reference menus by string ID, so registration order doesn't matter
5. **Standard DI Pattern**: Uses familiar `services.AddSingleton<IMenuProvider>(...)` pattern

### Example: Menu Configuration in Separate File

```csharp
// File: MenuConfiguration.cs
public static class MenuConfiguration
{
    public static void RegisterMenus(IServiceCollection services)
    {
        services.AddSingleton<IMenuProvider>(
            new MenuProvider("App.MainMenu", () => new MenuBuilder()
                .AddItem("File", cmd => BuildFileMenu())
                .AddItem("Edit", cmd => BuildEditMenu())
                .AddItem("View", cmd => BuildViewMenu()))
        );

        services.AddSingleton<IMenuProvider>(
            new ScopedMenuProvider("App.ContextMenu", sp, (builder, sp) =>
            {
                var documentService = sp.GetRequiredService<IDocumentService>();
                builder.AddItem("Save", cmd => documentService.Save());
            })
        );
    }

    private static MenuBuilder BuildFileMenu() { /* ... */ }
    private static MenuBuilder BuildEditMenu() { /* ... */ }
    private static MenuBuilder BuildViewMenu() { /* ... */ }
}

// File: Program.cs or Startup.cs
services.WithAura(options => options.WithDecorationSettings());
MenuConfiguration.RegisterMenus(services); // Called separately
```

### Benefits

| Benefit | Description |
|---------|-------------|
| **No Over-Engineering** | Uses existing DI patterns, no custom queueing mechanism needed |
| **Better Organization** | Menu logic lives in dedicated configuration files |
| **Compile-Time Safety** | Menu builder errors caught during configuration, not during `WithAura()` |
| **Testability** | Menu configurations can be unit tested independently |
| **Loose Coupling** | Windows only know menu IDs, not how/where menus are built |

## Conclusion

The revised Phase 10 design provides a **single, unified entry point** for Aura service registration that:

1. Reduces confusion by having one clear method: `WithAura()`
2. Distinguishes mandatory from optional features explicitly
3. Follows established best practices from the Config module
4. Prevents common DI anti-patterns like dual registration
5. Maintains backward compatibility with obsolete legacy methods
6. **Keeps menu provider registration simple** using standard DI patterns

This approach aligns with your goal: **"Ultimately, I want one single public helper: WithAura to register all Aura services and related DI things. This will reduce confusion of the developer using Aura."**

Menu providers remain flexible by using standard `IServiceCollection` registration, allowing them to be built and registered anywhere in the codebase without coupling to `WithAura()`.
