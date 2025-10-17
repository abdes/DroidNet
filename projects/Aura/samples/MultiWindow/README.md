# Aura Multi-Window Sample

This sample demonstrates Aura's comprehensive multi-window management capabilities for WinUI 3 applications, including the new WindowBackdropService for visual effects.

## Features Demonstrated

### 🪟 Multi-Window Management

- **Window Factory Pattern** - Create windows using dependency injection
- **Window Lifecycle Tracking** - Monitor creation, activation, and closure events
- **Window Context Management** - Track metadata, state, and window relationships

### 🎨 Theme Synchronization

- **Centralized Theme Coordination** - WindowManagerService manages all theme changes
- **Automatic Theme Application** - New windows receive current theme on creation
- **Cross-Window Theme Updates** - Theme changes propagate instantly to all open windows
- **Zero-Configuration Windows** - Individual windows don't need theme-handling code
- **Thread-Safe Updates** - Theme changes marshaled to UI thread automatically

### 🌈 Window Backdrop Effects (NEW)

- **WindowBackdropService** - Automatic backdrop application via event subscription
- **Multiple Backdrop Types** - None, Mica, MicaAlt, and Acrylic materials
- **Per-Window Configuration** - Select different backdrops for each window
- **Visual Material System** - Leverage WinUI 3's SystemBackdrop APIs

### 📑 Different Window Types

- **Main Windows** - Full-featured application windows
- **Tool Windows** - Lightweight utility windows
- **Document Windows** - Content editing windows with toolbars

### ⚡ Reactive Events

- **Observable Event Streams** - Subscribe to window lifecycle events using Rx
- **Real-time UI Updates** - Window list updates automatically on state changes
- **Event-Driven Architecture** - Clean separation of concerns

## Running the Sample

### Prerequisites

- .NET 9.0 SDK or later
- Windows 10 version 1809 (build 17763) or later
- Visual Studio 2022 (17.14 or later) recommended

### Build and Run

```powershell
# From the MultiWindow directory
dotnet run
```

Or set `Aura.MultiWindow.App` as the startup project in Visual Studio and press F5.

## Architecture

### Key Components

#### WindowManagerService

The core service managing window lifecycle:

- Creates windows using `IWindowFactory`
- Tracks all open windows in a thread-safe collection
- Publishes lifecycle events (Created, Activated, Closed)
- Applies themes to new windows automatically

#### WindowContext

Encapsulates window metadata:

- Unique identifier (GUID)
- Window title and type
- Creation and activation timestamps
- Custom metadata dictionary
- Activation state

#### IWindowFactory

Factory interface for creating windows:

- Resolves windows from DI container
- Supports generic and type-name-based creation
- Enables proper service injection into windows

### Service Registration

```csharp
var serviceCollection = new ServiceCollection();

// Add window management services
serviceCollection.AddAuraWindowManagement();

// Add backdrop service for automatic backdrop application
serviceCollection.AddSingleton<WindowBackdropService>();

// Register window types
serviceCollection.AddWindow<MainWindow>();
serviceCollection.AddWindow<ToolWindow>();
serviceCollection.AddWindow<DocumentWindow>();
```

### Backdrop Service Usage

The WindowBackdropService automatically observes window lifecycle events and applies backdrops based on window decoration settings:

```csharp
// Create a decoration with a specific backdrop
var decoration = new WindowDecorationOptions
{
    Category = WindowCategory.Tool,
    Backdrop = BackdropKind.Mica,
};

// Apply backdrop to window context
var backdropService = serviceProvider.GetRequiredService<WindowBackdropService>();
var decoratedContext = context with { Decoration = decoration };
backdropService.ApplyBackdrop(decoratedContext);
```

### Creating Windows

```csharp
// Create a window with metadata
var context = await windowManager.CreateWindowAsync<ToolWindow>(
    windowType: "Tool",
    title: "My Tool Window",
    metadata: new Dictionary<string, object>
    {
        ["Feature"] = "Calculator",
        ["Version"] = "1.0"
    });
```

### Subscribing to Events

```csharp
// Subscribe to all window events
windowManager.WindowEvents.Subscribe(evt =>
{
    switch (evt.EventType)
    {
        case WindowLifecycleEventType.Created:
            Console.WriteLine($"Window created: {evt.Context.Title}");
            break;
        case WindowLifecycleEventType.Activated:
            Console.WriteLine($"Window activated: {evt.Context.Title}");
            break;
        case WindowLifecycleEventType.Closed:
            Console.WriteLine($"Window closed: {evt.Context.Title}");
            break;
    }
});

// Filter for specific event types
windowManager.WindowEvents
    .Where(evt => evt.EventType == WindowLifecycleEventType.Created)
    .Subscribe(evt => HandleNewWindow(evt.Context));
```

## Keyboard Shortcuts

- **Ctrl+Shift+N** - Create new main window
- **Ctrl+Shift+T** - Create new tool window
- **Ctrl+Shift+D** - Create new document window
- **Ctrl+Shift+W** - Close all windows

## Code Highlights

### C# 13 Features Used

- **Primary Constructors** - Simplified constructor syntax
- **Collection Expressions** - Modern collection initialization `[]`
- **Field keyword** - Used in property accessors
- **Required members** - Ensure proper initialization

### Modern Patterns

- **Record types** - Immutable data models with value semantics
- **Pattern matching** - Enhanced switch expressions
- **Async/await** - Asynchronous window operations
- **Reactive Extensions** - Event streaming with System.Reactive
- **Source Generators** - CommunityToolkit.Mvvm for MVVM boilerplate

## Integration with Aura

This sample integrates with several Aura components:

### Centralized Theme Management

**Important Architecture Note:** Theme synchronization is handled entirely by the `WindowManagerService`. Individual windows do NOT need to subscribe to theme changes or handle theme application themselves.

- **WindowManagerService** - Automatically applies themes to all windows:
  - Subscribes to `AppearanceSettings.PropertyChanged`
  - Applies theme to new windows on creation
  - Propagates theme changes to all open windows instantly
  - Thread-safe with automatic UI thread marshaling

- **AppThemeModeService** - Low-level theme application API (called by WindowManagerService)

- **AppearanceSettingsService** - Persists theme preferences and fires PropertyChanged events

**Best Practice:** Window implementations should remain simple and focused on their specific UI. Don't add theme subscription code to individual windows - let WindowManagerService handle it centrally. This ensures:

- Single source of truth for theme coordination
- No duplicate subscriptions or race conditions
- Easier testing and maintenance
- Better performance (one subscription vs. N subscriptions)

### Other Aura Components

- **MainShellViewModel** - Can be used in any window
- **MainShellView** - Provides custom title bar and branding

## Extending the Sample

### Adding Custom Window Types

1. Create a new window class:

    ```csharp
    public sealed partial class CustomWindow : Window
    {
        public CustomWindow(IMyService service)
        {
            this.InitializeComponent();
            // Use injected services
        }
    }
    ```

2. Register the window:

    ```csharp
    serviceCollection.AddWindow<CustomWindow>();
    ```

3. Create instances:

    ```csharp
    await windowManager.CreateWindowAsync<CustomWindow>(
        windowType: "Custom",
        title: "My Custom Window");
    ```

### Custom Window Factory

Implement `IWindowFactory` for advanced scenarios:

```csharp
public class CustomWindowFactory : IWindowFactory
{
    public TWindow CreateWindow<TWindow>() where TWindow : Window
    {
        // Custom creation logic
        // E.g., window pooling, specialized initialization
    }
}

// Register custom factory
serviceCollection.AddAuraWindowManagement<CustomWindowFactory>();
```

## Related Documentation

- [Aura README](../../README.md) - Main Aura documentation
- [DroidNet.Routing](../../../Routing/README.md) - Navigation framework
- [DroidNet.Hosting](../../../Hosting/README.md) - Application hosting

## License

This sample is part of the DroidNet.Aura project and is licensed under the MIT License.
