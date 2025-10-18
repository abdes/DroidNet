# Menu Bar Configuration Guide

## Overview

The Aura framework supports configurable menu bars in window title bars with two display modes:

- **Standard Mode**: Traditional `MenuBar` that always displays all menu items
- **Expandable Mode**: Compact `ExpandableMenuBar` that starts with a hamburger button and expands on demand

## Configuration

The menu bar style is controlled by the `IsCompact` property in `MenuOptions`:

```csharp
// Standard persistent menu bar (IsCompact = false)
var decoration = WindowDecorationBuilder
    .ForMainWindow()
    .WithMenu("App.MainMenu", isCompact: false)
    .Build();

// Expandable hamburger menu (IsCompact = true)
var decoration = WindowDecorationBuilder
    .ForMainWindow()
    .WithMenu("App.MainMenu", isCompact: true)
    .Build();
```

## Persistence

The `IsCompact` setting is automatically persisted along with other window decoration options:

1. **Configuration File**: `Aura.json` in the application's config directory
2. **Serialization**: The `MenuOptionsJsonConverter` handles JSON serialization
3. **Settings Service**: The `WindowDecorationSettingsService` manages loading and saving

Example persisted JSON:

```json
{
  "categoryOverrides": {
    "Main": {
      "category": "Main",
      "chromeEnabled": true,
      "menu": {
        "menuProviderId": "App.MainMenu",
        "isCompact": false
      },
      "backdrop": "MicaAlt"
    }
  }
}
```

## Implementation Details

### XAML Bindings

The `MainShellView.xaml` uses compiled bindings (`{x:Bind}`) with converters to toggle visibility:

```xml
<!-- Standard persistent menu (when IsCompact=false) -->
<cm:MenuBar
    VerticalAlignment="Center"
    MenuSource="{x:Bind ViewModel.Context.MenuSource, Mode=OneWay}"
    Visibility="{x:Bind ViewModel.Context.Decoration.Menu.IsCompact, Mode=OneWay, Converter={StaticResource IsNotCompactToVis}}" />

<!-- Compact expandable menu (when IsCompact=true) -->
<cm:ExpandableMenuBar
    VerticalAlignment="Center"
    MenuSource="{x:Bind ViewModel.Context.MenuSource, Mode=OneWay}"
    Visibility="{x:Bind ViewModel.Context.Decoration.Menu.IsCompact, Mode=OneWay, Converter={StaticResource IsCompactToVis}}" />
```

### Property Change Notifications

The `Context` property in `MainShellViewModel` is an observable property (using `[ObservableProperty]` attribute), which ensures that:

- When the window context is assigned, property change notifications are raised
- The compiled XAML bindings automatically update
- The correct menu control (MenuBar or ExpandableMenuBar) becomes visible

## Key Fix Applied

**Issue**: The `IsCompact` property was not being taken into account because the `Context` property in `MainShellViewModel` was not observable.

**Solution**: Changed `Context` from a regular property to an observable property:

```csharp
// Before (not working)
public WindowContext? Context { get; private set; }

// After (working)
[ObservableProperty]
public partial WindowContext? Context { get; set; }
```

This ensures that when the window context is set during initialization, the XAML bindings are notified and update accordingly.

## Usage Example

```csharp
// In your Program.cs or configuration code
private static void ConfigureMainWindowDecoration(IContainer container)
{
    var decorationService = container.Resolve<ISettingsService<IWindowDecorationSettings>>();

    // Configure with standard menu bar
    var decoration = WindowDecorationBuilder
        .ForMainWindow()
        .WithMenu("App.MainMenu", isCompact: false)  // or true for expandable
        .Build();

    decorationService.Settings.SetCategoryOverride(WindowCategory.Main, decoration);
}
```

## Testing

To test the configuration:

1. **Change the setting** in your application startup code
2. **Run the application** - the menu bar should display in the configured mode
3. **Check persistence** - the setting should be saved to `Aura.json`
4. **Restart the application** - the saved setting should be loaded and applied

## Notes

- The `IsCompact` property is part of the `MenuOptions` record type
- Changes to `IsCompact` require creating a new `MenuOptions` instance (records are immutable)
- The setting is per window category (Main, Tool, Document, etc.)
- If no menu is configured (`Menu = null`), the menu region is hidden regardless of `IsCompact`
