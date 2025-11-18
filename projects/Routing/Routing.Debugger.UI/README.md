# Routing.Debugger.UI

A comprehensive UI framework for the DroidNet Routing Debugger application. This project provides views, view models, controls, and visual infrastructure for inspecting, testing, and debugging the DroidNet routing system through an interactive WinUI 3 interface.

## Table of Contents

- [Project Description](#project-description)
- [Technology Stack](#technology-stack)
- [Architecture](#architecture)
- [Getting Started](#getting-started)
- [Project Structure](#project-structure)
- [Key Features](#key-features)
- [Development Workflow](#development-workflow)
- [Coding Standards](#coding-standards)
- [Contributing](#contributing)
- [License](#license)

## Project Description

Routing.Debugger.UI is a specialized WinUI 3 project that delivers the visual interface and presentation logic for the DroidNet Routing Debugger. It enables developers to:

- Visualize routing configurations and route patterns in real-time
- Inspect and debug URL parsing and route matching
- Monitor router state changes during navigation
- Test complex multi-outlet navigation scenarios
- Explore the URL tree structure interactively
- Configure and manage application routes through an intuitive UI

The project follows MVVM patterns with source-generated view-model wiring and integrates with the DroidNet ecosystem including Routing, Hosting, Docking, and Aura components.

## Technology Stack

- **Language**: C# 13 (with nullable reference types enabled)
- **.NET Target Framework**: .NET 9.0 Windows (`net9.0-windows10.0.26100.0`)
- **UI Framework**: WinUI 3 with Microsoft.WindowsAppSDK 1.8+
- **Architecture Pattern**: MVVM (Model-View-ViewModel)

### Key Dependencies

**DroidNet Framework Components**:

- `Routing.Abstractions`: Routing interfaces and core models
- `Routing.WinUI`: WinUI integration for routing
- `Docking`: Flexible docking layout framework for multi-pane workspaces
- `Hosting`: .NET Generic Host integration with WinUI
- `Mvvm`: MVVM base classes and utilities
- `Mvvm.Generators`: Source generators for View-ViewModel wiring (with `[ViewModel]` attribute)
- `Converters`: Shared value converters
- `Aura`: Window management and theming services

**NuGet Packages**:

- `CommunityToolkit.Mvvm` 8.4: MVVM framework for bindings and observable objects
- `CommunityToolkit.WinUI.Controls.LayoutTransformControl`: Layout transformation controls
- `CommunityToolkit.WinUI.Controls.Sizers`: UI sizing utilities
- `CommunityToolkit.WinUI.Converters`: Common value converters
- `Microsoft.Xaml.Behaviors.WinUI.Managed`: XAML behaviors for WinUI
- `System.Reactive`: Reactive extensions for observable sequences
- `DryIoc.dll`: Lightweight dependency injection container

## Architecture

### Layer Structure

```text
┌──────────────────────────────────────────┐
│     XAML Views & WinUI Controls          │
│  (Config, UrlTree, State, Workspace)     │
├──────────────────────────────────────────┤
│     ViewModels (MVVM)                    │
│  (Source-generated wiring via            │
│   Mvvm.Generators)                       │
├──────────────────────────────────────────┤
│     Adapters & Business Logic            │
│  (RouteAdapter, RouterStateAdapter)      │
├──────────────────────────────────────────┤
│     Domain Services                      │
│  (Router, Docking, Hosting)              │
└──────────────────────────────────────────┘
```

### Key Component Groups

- **Shell** (`Shell/`): Main application window layout and shell view model
- **Config** (`Config/`): Route configuration display and management views
- **UrlTree** (`UrlTree/`): URL parsing visualization and tree control
- **State** (`State/`): Router state inspection and display
- **Workspace** (`WorkSpace/`): Multi-pane debugging workspace with docking integration
- **Docks** (`Docks/`): Custom dockable pane implementations
- **TreeView** (`TreeView/`): Reusable expandable tree control framework
- **Styles** (`Styles/`): Custom styling and visual converters
- **Assets** (`Assets/`): Images, icons, and XAML resources

### View-ViewModel Wiring

Views use the `[ViewModel(typeof(TViewModel))]` attribute (from `Mvvm.Generators.Attributes`) for source-generated wiring, eliminating manual DataContext binding boilerplate.

## Getting Started

### Prerequisites

- .NET 9.0 or later
- Visual Studio 2022 (or later) with WinUI development support
- Windows SDK 26100.0 or later

### Setup Instructions

1. **Clone and restore** the DroidNet repository:

   ```powershell
   git clone https://github.com/abdes/DroidNet.git
   cd DroidNet
   pwsh ./init.ps1  # Run once to set up .NET SDK and tools
   ```

2. **Build the project**:

   ```powershell
   dotnet build projects/Routing/Routing.Debugger.UI/src/Routing.Debugger.UI.csproj
   ```

3. **Open in Visual Studio**:

   ```powershell
   cd projects
   .\open.cmd
   ```

   This generates `Projects.sln` and opens it in Visual Studio.

## Project Structure

```text
src/
  ├── Shell/                    # Main application shell (layout, window)
  │   ├── ShellView.xaml        # Shell UI definition
  │   ├── ShellViewModel.cs     # Shell presentation logic
  │   ├── TopNavBar.xaml        # Top navigation bar component
  │   └── TopNavBarButton.cs    # Navigation button styling
  │
  ├── Config/                   # Route configuration display
  │   ├── RoutesView.xaml       # Routes configuration UI
  │   ├── RoutesViewModel.cs    # Routes configuration logic
  │   └── RouteAdapter.cs       # Adapts routing data for display
  │
  ├── UrlTree/                  # URL parsing and tree visualization
  │   ├── UrlTreeView.xaml      # URL tree UI
  │   ├── UrlTreeViewModel.cs   # URL tree logic
  │   └── UrlSegmentGroupAdapter.cs  # Adapts URL segments for display
  │
  ├── State/                    # Router state inspection
  │   ├── RouterStateView.xaml  # State display UI
  │   ├── RouterStateViewModel.cs  # State presentation logic
  │   └── RouterStateAdapter.cs # Adapts router state for display
  │
  ├── WorkSpace/                # Multi-pane workspace with docking
  │   ├── WorkSpaceView.cs      # Workspace UI composition
  │   ├── WorkSpaceViewModel.cs # Workspace layout management
  │   ├── DockViewFactory.cs    # Factory for creating dock views
  │   ├── ApplicationDock.cs    # Application dock container
  │   ├── RoutedDockable.cs     # Routed dockable panel
  │   └── DockExtensions.cs     # Docking extension methods
  │
  ├── Docks/                    # Custom dockable pane implementations
  │   ├── EmbeddedAppView.xaml  # Embedded application view
  │   └── EmbeddedAppViewModel.cs
  │
  ├── TreeView/                 # Reusable tree control framework
  │   ├── TreeItemControl.xaml  # Tree item presentation
  │   ├── ExpandingTreeControl.xaml  # Expandable tree root
  │   ├── TreeViewModelBase.cs  # Base class for tree view models
  │   ├── TreeItemAdapterBase.cs    # Base for tree item adapters
  │   ├── ExpanderIconConverter.cs  # Expansion state converter
  │   └── IndentToMarginConverter.cs # Tree indentation converter
  │
  ├── Styles/                   # Styling and visual resources
  │   ├── Styles.xaml           # XAML style definitions
  │   ├── TopNavBarButtonRow.cs # Button row styling
  │   ├── TopNavBarUrlBox.cs    # URL input styling
  │   ├── ItemProperty.cs       # Property display styling
  │   └── ItemProperties.cs     # Multiple properties display
  │
  ├── Assets/                   # Images and resources
  ├── Icons/                    # Application icons
  │
  ├── DebuggerConstants.cs      # Application-wide constants
  └── Routing.Debugger.UI.csproj  # Project file

```

## Key Features

- **Interactive Route Visualization**: Display routing configurations with expandable tree views
- **URL Parsing**: Visualize URL structure and segment breakdown
- **State Inspection**: Monitor router state and navigation context in real-time
- **Multi-Pane Workspace**: Docking-enabled layout for flexible debugger pane arrangement
- **MVVM Architecture**: Clean separation of concerns using view models and data binding
- **Source-Generated Wiring**: Compile-time view-model linking via `Mvvm.Generators`
- **Custom Controls**: Reusable tree control framework with indent converters and expander icons
- **Styling Framework**: Centralized WinUI styling with property-based item customization

## Development Workflow

### Building

Build only this project for faster iteration:

```powershell
dotnet build projects/Routing/Routing.Debugger.UI/src/Routing.Debugger.UI.csproj
```

Or build the entire Routing module:

```powershell
dotnet build projects/Routing/Routing.Debugger.UI/src/Routing.Debugger.UI.csproj
```

### Running the Debugger Application

The Routing.Debugger.UI library is consumed by the Routing.Debugger application. To run the full debugger:

```powershell
dotnet run --project projects/Routing/Routing.Debugger/src/Routing.Debugger.csproj
```

### File Organization

- **Source files** go in `src/` organized by feature area (Shell, Config, UrlTree, etc.)
- **Each feature area** contains Views (`.xaml`), ViewModels (`.cs`), and Adapters (`.cs`)
- **Shared utilities** are in `TreeView/` and `Styles/`

## Coding Standards

This project follows the DroidNet repository coding standards:

- **C# 13** with nullable reference types enabled
- **Explicit access modifiers** required (`public`, `private`, `internal`, etc.)
- **`this.`** prefix for instance members
- **Composition** over inheritance; small, well-justified APIs
- **MVVM patterns**: Views are `partial class` with `[ViewModel(typeof(TViewModel))]` attribute
- **No code-behind**: Minimize code-behind in XAML; use view models and source generators
- **Naming conventions**:
  - Views: `*View.xaml` and `*View.xaml.cs` (partial class)
  - ViewModels: `*ViewModel.cs` inheriting from `ObservableObject` or framework base
  - Adapters: `*Adapter.cs` for data transformation between domain and UI
- **StyleCop & Analyzers**: Code must pass StyleCop.Analyzers, Roslynator, and Meziantou.Analyzer rules

For detailed style guidelines, see [`.github/instructions/csharp_coding_style.instructions.md`](../../.github/instructions/csharp_coding_style.instructions.md) in the repository root.

## Contributing

Contributions to Routing.Debugger.UI are welcome! Please follow these guidelines:

1. **Understand the architecture**: Review the Shell, Config, and UrlTree modules to understand MVVM wiring and routing integration
2. **Follow MVVM patterns**: Keep business logic in view models; views should be minimal
3. **Use source-generated wiring**: Decorate views with `[ViewModel(typeof(TViewModel))]` instead of manual DataContext assignment
4. **Test thoroughly**: Verify UI interactions and state management work correctly
5. **Respect the docking framework**: When adding new panes, integrate with `DockViewFactory` and `IDockable`
6. **Document complex logic**: Comment why, not what; explain router state handling and outlet transitions

### Pull Request Process

- Ensure code follows coding standards and analyzer rules pass
- Include any necessary updates to project structure documentation
- Test the debugger UI with various routing scenarios

## License

This project is part of the DroidNet framework and is distributed under the **MIT License**. See the [LICENSE](../../../../LICENSE) file in the repository root for details.

---

**Related Projects**:

- [`Routing`](../Routing.Router): Core routing engine
- [`Routing.WinUI`](../Routing.WinUI): WinUI routing integration
- [`Routing.Debugger`](../Routing.Debugger): Main debugger application
- [`Docking`](../../Docking): Docking framework documentation
- [`Hosting`](../../Hosting): .NET Generic Host integration
