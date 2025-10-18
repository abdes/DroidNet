# Phase 10: Menu Provider Registration Decision

## Question

Should menu providers be registered via `WithAura()` fluent API or separately using standard DI patterns?

## Decision: Use Standard DI Patterns (Separate Registration)

Menu providers will **NOT** be part of `AuraOptions`. Instead, they use standard `IServiceCollection` registration.

## Rationale

### ✅ Advantages of Separate Registration

1. **Separation of Concerns**: Menu building logic decoupled from service registration
2. **Flexibility**: Menus can be built in dedicated configuration classes
3. **No Over-Engineering**: Uses existing DI patterns, no custom queueing needed
4. **Timing Independence**: Can register before or after `WithAura()`
5. **Better Organization**: Menu logic lives in dedicated files like `MenuConfiguration.cs`
6. **Loose Coupling**: Windows reference menus by ID only

### ❌ Disadvantages of Inline Registration

1. Couples menu building to `WithAura()` call
2. Makes startup code cluttered with menu logic
3. Requires custom queueing mechanism in `AuraOptions`
4. Forces all menu providers to be registered at same time
5. Harder to organize complex menu hierarchies

## Implementation

### Rejected Approach (Inline)

```csharp
services.WithAura(options => options
    .WithMenuProvider("App.MainMenu", () => new MenuBuilder()
        .AddItem("File", ...)
        .AddItem("Edit", ...)
        .AddItem("View", ...))  // Too much code in WithAura()
);
```

### Approved Approach (Separate)

```csharp
// Program.cs - Clean and focused
services.WithAura(options => options
    .WithDecorationSettings()
    .WithBackdropService()
);

// MenuConfiguration.cs - Dedicated menu logic
public static class MenuConfiguration
{
    public static void RegisterMenus(IServiceCollection services)
    {
        services.AddSingleton<IMenuProvider>(
            new MenuProvider("App.MainMenu", () => BuildMainMenu())
        );

        services.AddSingleton<IMenuProvider>(
            new MenuProvider("App.ContextMenu", () => BuildContextMenu())
        );
    }

    private static MenuBuilder BuildMainMenu()
    {
        return new MenuBuilder()
            .AddItem("File", cmd => BuildFileSubmenu())
            .AddItem("Edit", cmd => BuildEditSubmenu())
            .AddItem("View", cmd => BuildViewSubmenu());
    }

    // ... more menu building methods
}

// Program.cs - Register menus
MenuConfiguration.RegisterMenus(services);
```

## Key Benefits

1. **Simple**: No special `AuraOptions` methods needed for menu providers
2. **Flexible**: Menu configuration can be split across multiple files/classes
3. **Standard**: Uses familiar DI patterns developers already know
4. **Testable**: Menu configurations can be unit tested independently
5. **ID-Based**: Windows only reference menu IDs, registration location doesn't matter

## Updated Task List

**Removed Tasks**:

- ❌ TASK-077: Add `WithMenuProvider()` to `AuraOptions`
- ❌ TASK-078: Add `WithScopedMenuProvider()` to `AuraOptions`
- ❌ TASK-085: Register queued menu providers

**Simplified Tasks**:

- Task count reduced from 15 to 12 tasks
- No custom queueing logic needed in `AuraOptions`
- Cleaner implementation with less code

## Documentation Updates

XML documentation for `WithAura()` will include examples showing:

- How to register menu providers separately
- Recommended pattern using dedicated `MenuConfiguration` class
- Note that menu providers can be registered anywhere in startup code

## Conclusion

This decision **avoids over-engineering** by:

- Not creating a custom menu provider queueing system
- Using standard DI patterns developers already understand
- Keeping `WithAura()` focused on core Aura services only
- Allowing maximum flexibility for menu organization

The ID-based lookup in `WindowContextFactory` already provides the loose coupling needed - no special registration mechanism required.
