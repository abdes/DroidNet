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

- **DroidNet.Config** - Configuration management
- **DroidNet.Controls.Menus** - Menu system integration
- **DroidNet.Hosting** - Application hosting and lifecycle
- **DroidNet.Routing** - URI-based navigation
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

### Architecture Highlights

- **Reactive Programming** - Uses System.Reactive (Rx) for event handling
- **Dependency Injection** - All services are registered and injected via DryIoc
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

- **Service Registration** - Easy service configuration via Host builder
- **Scoped Lifetimes** - Proper service lifetime management
- **Factory Support** - Custom factory patterns for complex dependencies

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

2. **Configure services** in your `Program.cs`:

    ```csharp
    var bootstrap = new Bootstrapper(args);
    bootstrap.Configure()
        .WithLoggingAbstraction()
        .WithConfiguration(...)
        .WithMvvm()
        .WithRouting(routes)
        .WithWinUI<App>()
        .WithAppServices(services =>
        {
            // Register Aura services
            services.AddSingleton<IAppThemeModeService, AppThemeModeService>();
            services.AddSingleton<AppearanceSettingsService>();
        });
    ```

3. **Use MainShellView** as your root:

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
- **FluentAssertions** for readable assertions
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
