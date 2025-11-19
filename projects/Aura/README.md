# Aura

[![Windows][windows-badge]][WinUI]
[![.NET 9.0][dotnet-badge]][dotnet]
[![License: MIT][license-badge]][license]

> A comprehensive WinUI 3 shell framework providing window management, theming, and enhanced UI decorations for Windows applications.

[windows-badge]: https://img.shields.io/badge/OS-Windows-blue
[WinUI]: https://learn.microsoft.com/en-us/windows/apps/winui/
[dotnet-badge]: https://img.shields.io/badge/.NET-9.0-512BD4
[dotnet]: https://dotnet.microsoft.com/download
[license-badge]: https://img.shields.io/badge/License-MIT-yellow.svg
[license]: https://opensource.org/licenses/MIT

## Overview

**Aura** provides the foundational shell infrastructure for WinUI 3 applications, handling everything around the core business logic—including window management, custom decorations, theme support, OS notifications, and taskbar integration. It's designed to work seamlessly within the DroidNet ecosystem but can also be used independently in any WinUI 3 project.

Aura serves as the outer layer of your application, providing a polished, customizable user interface shell that embraces the MVVM architectural pattern and integrates deeply with WinUI 3's modern design principles.

## Technology Stack

### Core Technologies

- **.NET 9.0** - Targeting `net9.0-windows10.0.26100.0`
- **WinUI 3** - Windows App SDK 1.8
- **C# Preview Features** - Latest language features with strict mode enabled
- **XAML** - Declarative UI design

### Key Dependencies

- **Microsoft.WindowsAppSDK** (1.8.251003001) - Core WinUI 3 framework
- **CommunityToolkit.Mvvm** (8.4.0) - MVVM infrastructure and source generators
- **WinUIEx** (2.9.0) - Extended WinUI utilities
- **Microsoft.Extensions.Logging.Abstractions** (9.0.10) - Logging infrastructure
- **DryIoc** (6.0.0) - Dependency injection container
- **Serilog** (4.1.0) - Structured logging

### DroidNet Integration

Aura integrates with several DroidNet components:

- **DroidNet.Config** - Configuration management and `ISettingsService<T>` wrappers
- **DroidNet.Controls.Menus** - Menu system integration (`IMenuProvider` / `IMenuSource`)
- **DroidNet.Hosting** - Application hosting and lifecycle (wires the DryIoc container)
- **DroidNet.Routing** - URI-based navigation and activation events
- **DroidNet.Mvvm.Generators** - Custom MVVM source generators

## Project Architecture

Aura follows a **ViewModel-First MVVM** architecture with clear separation of concerns:

### Core Components

#### MainShellViewModel

The central coordinator managing:

- Window lifecycle and events
- Appearance settings and theme synchronization
- Menu management (settings, theme selection)
- Router outlet containers for navigation
- Custom title bar setup

#### MainShellView

The visual representation providing:

- Custom title bar with adaptive layout
- Application icon and branding
- Menu bar and flyout integration
- Router outlet for content injection
- Responsive design with visual state management

#### AppThemeModeService

Implements `IAppThemeModeService` to:

- Apply theme modes (Light/Dark/System) to windows
- Manage title bar theming
- Handle appearance setting changes reactively

#### AppearanceSettingsService

Extends `SettingsService<AppearanceSettings>` to:

- Persist theme preferences
- Notify observers of theme changes
- Provide `IAppearanceSettings` interface

#### Window Manager & Window Factory

The windowing subsystem provides a consistent, testable API for creating, registering, and managing WinUI `Window` instances.

- `IWindowFactory` / `DefaultWindowFactory` — a DI-friendly factory that resolves windows from the container (including keyed registrations) and optionally applies decorations based on a `WindowCategory`.
- `IWindowManagerService` / `WindowManagerService` — coordinates windows that are registered with Aura, exposing programmatic control and lifecycle events. It tracks windows as `ManagedWindow` contexts which contain metadata, decoration details, and presenter state.

Key responsibilities of the manager and factory include:

- Creating and registering windows with DI and the manager (generic or keyed resolution).
- Applying category-specific decoration and menu integration via configured `WindowDecorationOptions`.
- Exposing lifecycle events and observables for creation, activation, deactivation, presenter state change, bounds changes and closure.
- Managing per-window metadata and menu providers resolved from DI.
- Integrating with the `IRouter` for windows created by route targets so router-based windows are automatically tracked.

A `ManagedWindow` is the context object representing a registered window. It exposes:

- `Id` — the unique `WindowId` for programmatic lookup.
- `Window` — the underlying WinUI `Window` instance.
- `Category`, `Decorations` and `MenuSource` — decoration metadata and any created menu source (via `IMenuProvider`).
- `PresenterState`, `CurrentBounds`, and optional `RestoredBounds` — current presenter state and bounds management.
- `Metadata` — a dictionary for custom values attached to a specific window that consumers can query or change via the manager API.

Decorations are resolved from `WindowDecorationOptions` and may include details like custom chrome, backdrop preferences, and menu provider identifiers. When a menu provider ID is present in the decoration, the menu provider is resolved from DI and a `MenuSource` is created and attached to the `ManagedWindow` context for the lifetime of the window.

### Architecture Highlights

- **Reactive Programming** - Uses System.Reactive (Rx) for event handling
- **Dependency Injection** - Primary integration via DryIoc; use the `IContainer.WithAura()` extension to register Aura features
- **Source Generators** - Leverages custom MVVM generators for boilerplate reduction
- **Outlet-Based Navigation** - Integrates with DroidNet routing for view composition

## Getting Started

### Prerequisites

- **Windows 10** version 1809 (build 17763) or later
- **.NET 9.0 SDK** or later
- **Visual Studio 2022** (17.14 or later) with:
  - Windows App SDK workload
  - .NET desktop development workload
  - Universal Windows Platform development workload (optional)

### Installation

#### As Part of DroidNet Monorepo

1. Clone the DroidNet repository:

    ```powershell
    git clone https://github.com/abdes/DroidNet.git
    cd DroidNet/projects/Aura
    ```

2. Generate and open the solution using the provided script:

    ```powershell
    .\open.cmd
    ```

    This script uses the [SlnGen](https://microsoft.github.io/slngen/) .NET tool to dynamically generate the `Aura.sln` solution file from all `*.csproj` files in the directory tree, then opens it in Visual Studio.

> **Note:** The solution file (`Aura.sln`) is generated on-demand and not checked into source control. Always use `open.cmd` to ensure you have the latest project references.

#### As a Standalone Package

Add the NuGet package reference to your project:

```xml
<PackageReference Include="DroidNet.Aura" Version="1.0.0-alpha" />
```

### Building the Project

#### Using Visual Studio

1. Generate and open the solution:

    ```powershell
    .\open.cmd
    ```

2. Build the solution: `Ctrl+Shift+B`

#### Using Command Line

Generate the solution file first, then build:

```powershell
# Generate the solution file using SlnGen
dotnet slngen -d . -o Aura.sln --folders false .\**\*.csproj

# Build the solution
dotnet build Aura.sln
```

Or simply:

```powershell
# Build all projects without a solution file
dotnet build
```

### Running the Sample Application

The SingleWindow sample demonstrates Aura in action:

1. Navigate to the sample directory:

    ```powershell
    cd samples/SingleWindow
    ```

2. Run the application:

    ```powershell
    dotnet run
    ```

    The sample project is pre-configured with `TargetFramework` and `RuntimeIdentifier` set to `win-x64`, so no additional parameters are needed.

Or set `Aura.SingleWindow.App` as the startup project in Visual Studio and press `F5`.

## Project Structure

``` text
Aura/
├── src/                          # Core library source
│   ├── Aura.csproj              # Main project file
│   ├── MainShellView.xaml       # Shell view (UI)
│   ├── MainShellViewModel.cs    # Shell view model
│   ├── MainWindow.xaml          # Main window host
│   ├── AppThemeModeService.cs   # Theme management service
│   ├── AppearanceSettings*.cs   # Settings infrastructure
│   ├── Windowing/               # Window manager, factories and related types
│   ├── Assets/                  # Icons and resources
│   └── Themes/                  # Theme resource dictionaries
├── samples/
│   └── SingleWindow/            # Sample application
│       ├── Program.cs           # Custom entry point with Host
│       ├── App.xaml             # Application definition
│       └── Aura.SingleWindow.App.csproj
└── README.md
```

## Key Features

### 🎨 Theme Management

- **Light/Dark/System Modes** - Full support for Windows theme preferences
- **Dynamic Theme Switching** - Change themes at runtime without restart
- **Custom Title Bar Theming** - Automatically themed title bars with custom colors
- **Persistent Settings** - Theme preferences saved and restored across sessions

### 🪟 Enhanced Window Decorations

- **Custom Title Bars** - Branded title bars with application icons
- **Adaptive Layouts** - Responsive design that adapts to window size
- **Menu Integration** - Built-in menu bar and flyout support
- **Drag Regions** - Proper window dragging areas

### 🪟 Window Management & Lifecycle

- **DI-based Window Creation** - Use `IWindowFactory` to resolve windows using DI. The `DefaultWindowFactory` supports generic creation, keyed registrations, and decorated window creation using `WindowCategory`.
- **Manager-Tracked Windows** - Use `IWindowManagerService` to register and track windows; windows are represented by `ManagedWindow` which exposes `Id`, `Category`, `Decorations`, `MenuSource`, `Metadata`, `PresenterState` and other helpful properties.
- **Lifecycle Events & Observables** - Subscribe to lifecycle events via `IWindowManagerService.WindowEvents` (an `IObservable<WindowLifecycleEvent>`) or async event handlers (`PresenterStateChanging`, `PresenterStateChanged`, `WindowClosing`, `WindowClosed`, `WindowBoundsChanged`). Event types include: `WindowLifecycleEventType.Created`, `WindowLifecycleEventType.Activated`, `WindowLifecycleEventType.Deactivated`, and `WindowLifecycleEventType.Closed`.
- **Programmatic Control** - Programmatically activate, minimize, maximize, restore and close windows using the manager API (`ActivateWindow`, `MinimizeWindowAsync`, `MaximizeWindowAsync`, `RestoreWindowAsync`, `CloseWindowAsync`).
- **Metadata & Menu Providers** - Attach arbitrary metadata to windows using `SetMetadata` and integrate UI menus using `IMenuProvider` when decoration contains a menu provider ID.
- **Router Integration** - The `WindowManagerService` integrates with the `IRouter` to automatically track windows created by route targets and attach route metadata.

### 🎯 MVVM Architecture

- **ViewModel-First** - ViewModels drive the UI with automatic view location
- **Source Generators** - Reduce boilerplate with compile-time code generation
- **Observable Properties** - Reactive UI updates using CommunityToolkit.Mvvm
- **Command Pattern** - Clean separation of UI actions from business logic

### 🧭 Router Integration

- **URI-Based Navigation** - Navigate using familiar URI patterns
- **Outlet System** - Multiple router outlets for complex layouts
- **Route Guards** - Control navigation flow with guards and resolvers
- **Event Streaming** - Reactive navigation events using Rx

### 🔧 Dependency Injection

- **Container registration** - Use the `IContainer.WithAura()` DryIoc extension to register Aura core services and optional features (`AuraOptions`). Hosting integration in `DroidNet.Hosting` wires the application container to the host when used.
- **Scoped Lifetimes** - Proper service lifetime management
- **Factory Support** - Custom factory patterns for complex dependencies

### Documents & TabStrip Drag

- **Document integration** - Aura expects the host application to implement `IDocumentService` (see `src/Documents/IDocumentService.cs`). Aura's `DocumentTabPresenter` wires the document events to the `TabStrip` control and forwards user interactions (selection, close, detach) back to the application-level document service.
- **Tab tear-out & drag** - The `TabStrip` control supports reordering, tear-out (creating a new host window), cross-window drag and drop, and a coordinated `ITabDragCoordinator`/`IDragVisualService` pair for rich visuals and resilient cross-window dragging. See `src/Drag/README.md` for the full specification and event contract (e.g. `TabCloseRequested`, `TabDragImageRequest`, `TabTearOutRequested`, `TabDragComplete`).

## Development Workflow

### Solution Generation

Aura uses [SlnGen](https://microsoft.github.io/slngen/) to dynamically generate Visual Studio solution files. This approach keeps the repository clean and ensures the solution always reflects the current project structure.

```powershell
# Generate solution and open in Visual Studio
.\open.cmd

# Or generate manually
dotnet slngen -d . -o Aura.sln --folders false .\**\*.csproj
```

**Benefits of SlnGen:**

- Solution files are always up-to-date
- No merge conflicts on solution files
- Automatic discovery of new projects
- Consistent experience across the monorepo

### Building

```powershell
# Build all projects (no solution file needed)
dotnet build

# Build with generated solution
.\open.cmd  # Generate solution first
dotnet build Aura.sln

# Build for release
dotnet build -c Release
```

### Running

```powershell
# Run the sample application
cd samples/SingleWindow
dotnet run

# Or run from the root with explicit project path
dotnet run --project samples/SingleWindow/Aura.SingleWindow.App.csproj

# Or use Visual Studio
# Set startup project and press F5
```

> **Note:** WinUI 3 applications require a runtime identifier. The sample project has `RuntimeIdentifier` set to `win-x64` for simplified `dotnet run` usage.

### Creating a New Shell

1. **Reference the package** in your WinUI project:

    ```xml
    <PackageReference Include="DroidNet.Aura" Version="1.0.0-alpha" />
    ```

2. **Register Aura into your DryIoc container.** Aura exposes a fluent `WithAura()` extension on `DryIoc.IContainer` to register mandatory services and optional features via `AuraOptions`:

    ```csharp
    using DryIoc;

    var container = new Container();

    // Minimal registration (mandatory services only)
    container.WithAura();

    // Full registration with optional features
    container.WithAura(options => options
        .WithDecorationSettings()
        .WithAppearanceSettings()
        .WithBackdropService()
        .WithThemeModeService()
        .WithDrag()
    );

    // Register windows for factory resolution
    container.AddWindow<RoutedWindow>();

    // Register menu providers separately as singletons implementing IMenuProvider
    // container.Register<IMenuProvider>(Made.Of(() => new MenuProvider(...)), Reuse.Singleton);
    ```

3. **Use `MainShellView`** as your root view in XAML. The sample uses a host `Window` with the shell view inserted as content:

    ```xaml
    <local:MainWindow
        xmlns:aura="using:DroidNet.Aura">
        <aura:MainShellView DataContext="{Binding MainShellViewModel}" />
    </local:MainWindow>
    ```

## Usage Examples

### Customizing Themes

Apply a theme to a window programmatically:

```csharp
public class MyViewModel
{
    private readonly IAppThemeModeService themeService;

    public MyViewModel(IAppThemeModeService themeService)
    {
        this.themeService = themeService;
    }

    public void ApplyDarkTheme(Window window)
    {
        themeService.ApplyThemeMode(window, ElementTheme.Dark);
    }
}
```

### Managing Appearance Settings

Access and modify appearance settings:

```csharp
public class SettingsViewModel
{
    private readonly AppearanceSettingsService appearanceSettings;

    public SettingsViewModel(AppearanceSettingsService appearanceSettings)
    {
        this.appearanceSettings = appearanceSettings;

        // Listen for theme changes
        appearanceSettings.PropertyChanged += OnThemeChanged;
    }

    public ElementTheme CurrentTheme
    {
        get => appearanceSettings.AppThemeMode;
        set => appearanceSettings.AppThemeMode = value;
    }
}
```

### Building Custom Menus

Create dynamic menus using MenuBuilder:

```csharp
var menuBuilder = new MenuBuilder();

// Add menu items
menuBuilder.AddMenuItem(new MenuItemData
{
    Text = "Settings",
    Icon = new SymbolIcon(Symbol.Setting),
    Command = SettingsCommand
});

menuBuilder.AddSeparator();

menuBuilder.AddMenuItem(new MenuItemData
{
    Text = "Exit",
    Command = ExitCommand
});

// Build the menu
var menuSource = menuBuilder.Build();
```

### Extending MainShellViewModel

Create a custom shell by inheriting:

```csharp
public partial class MyCustomShellViewModel : MainShellViewModel
{
    public MyCustomShellViewModel(
        IRouter router,
        HostingContext hostingContext,
        AppearanceSettingsService appearanceSettings,
        IMyCustomService customService)
        : base(router, hostingContext, appearanceSettings)
    {
        // Add custom initialization
    }

    // Add custom properties and commands
    [ObservableProperty]
    private string customProperty;
}
```

### Window Management Examples

Create a decorated window using the DI-friendly factory (factory registers the window with the manager automatically):

```csharp
var windowFactory = serviceProvider.GetRequiredService<IWindowFactory>();
var window = await windowFactory.CreateDecoratedWindow<MyWindow>(WindowCategory.Main);
// Show / activate the window
window.Activate();
```

Register a manually created window with the manager and attach metadata:

```csharp
var windowManager = serviceProvider.GetRequiredService<IWindowManagerService>();
var context = await windowManager.RegisterDecoratedWindowAsync(window, WindowCategory.Secondary, new Dictionary<string, object>
{
    ["CreatedBy"] = "ManualHost",
    ["RoutingTarget"] = "Settings",
});

// Set metadata and close the window through the manager
await windowManager.SetMetadataAsync(context.Id, "LastSeenUser", "user123");
await windowManager.CloseWindowAsync(context.Id);
```

Subscribe to lifecycle events and presenter state changes:

```csharp
var subscription = windowManager.WindowEvents.Subscribe(evt =>
{
    Console.WriteLine($"Window {evt.Context.Id.Value} lifecycle: {evt.EventType}");
});

windowManager.PresenterStateChanged += async (s, e) =>
{
    Console.WriteLine($"Presenter state changed from {e.OldState} to {e.NewState}");
};
```

## Coding Standards

This project adheres to strict coding standards enforced through analyzers:

### Code Quality

- **StyleCop** - Comprehensive style rules
- **Roslynator** - Advanced code analysis and refactoring
- **Meziantou.Analyzer** - Best practices enforcement
- **.NET Analyzers** - Framework-specific guidelines

### Language Features

- **C# Preview** with strict mode
- **Nullable Reference Types** enabled
- **Implicit Usings** enabled
- **File-Scoped Namespaces**

### Documentation

- XML documentation required for public APIs
- MIT license header on all source files
- Copyright attribution to "The Authors"

### Code Style

```csharp
// File-scoped namespaces
namespace DroidNet.Aura;

// Partial classes with source generators
public partial class MyViewModel : ObservableObject
{
    // Observable properties via attributes
    [ObservableProperty]
    private string myProperty;

    // Commands via attributes
    [RelayCommand]
    private void ExecuteAction()
    {
        // Implementation
    }
}
```

## Testing

While this library currently focuses on the core shell functionality, testing patterns follow DroidNet standards:

### Testing Approach

- **MSTest** framework for unit tests
- **AwesomeAssertions** for readable assertions
- **Moq** for mocking dependencies
- **Testably.Abstractions** for filesystem abstractions

### Future Testing

Test projects will be added to cover:

- Theme service functionality
- Settings persistence
- View model behavior
- Menu building logic

## Contributing

Contributions are welcome! Aura is part of the larger DroidNet project. Here's how to contribute:

### Development Setup

1. Fork the repository
2. Clone your fork and navigate to the Aura project
3. Generate the solution: `.\open.cmd`
4. Create a feature branch: `git checkout -b feature/amazing-feature`
5. Make your changes following the coding standards
6. Ensure all analyzers pass with zero warnings
7. Commit your changes: `git commit -m 'Add amazing feature'`
8. Push to the branch: `git push origin feature/amazing-feature`
9. Open a Pull Request

> **Note:** Do not commit the generated `Aura.sln` file. It's automatically generated by SlnGen and excluded from source control.

### Guidelines

- Follow the existing code style and architecture patterns
- Add XML documentation for public APIs
- Include the MIT license header in new files
- Ensure nullable reference types are properly handled
- Use source generators where applicable (CommunityToolkit.Mvvm)

### Code Review Process

All submissions require review. We use GitHub pull requests for this purpose. Consult [GitHub Help](https://help.github.com/articles/about-pull-requests/) for more information on using pull requests.

## License

This project is licensed under the **MIT License** - see the [LICENSE](../../LICENSE) file for details.

``` text
Copyright (c) 2024 Abdessattar Sassi

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
```

See [LICENSE](../../LICENSE) for the full license text.

## Related Projects

Aura is part of the **DroidNet** ecosystem:

- **[DroidNet.Routing](../Routing/)** - URI-based navigation framework
- **[DroidNet.Docking](../Docking/)** - Flexible docking framework for WinUI 3
- **[DroidNet.Hosting](../Hosting/)** - Application hosting and DI integration
- **[DroidNet.Controls.Menus](../Controls/Menus/)** - Advanced menu controls
- **[DroidNet.Mvvm](../Mvvm/)** - MVVM infrastructure and patterns

## Acknowledgments

- Built with [WinUI 3](https://learn.microsoft.com/en-us/windows/apps/winui/) and the Windows App SDK
- Uses [CommunityToolkit.Mvvm](https://learn.microsoft.com/en-us/dotnet/communitytoolkit/mvvm/) for MVVM infrastructure
- Powered by [DryIoc](https://github.com/dadhi/DryIoc) for dependency injection
- Enhanced with [WinUIEx](https://github.com/dotMorten/WinUIEx) utilities
