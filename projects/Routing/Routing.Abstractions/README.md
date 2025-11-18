# Routing Abstractions

DroidNet.Routing is an Angular-inspired URL-based routing and navigation system for WinUI applications built on .NET. The `Routing.Abstractions` library provides the core interfaces, models, and abstractions that define the routing system architecture, enabling sophisticated navigation between views with full support for multiple navigation contexts, dynamic state management, and advanced URL pattern matching.

## Table of Contents

- [Project Description](#project-description)
- [Technology Stack](#technology-stack)
- [Architecture](#architecture)
- [Getting Started](#getting-started)
- [Project Structure](#project-structure)
- [Core Features](#core-features)
- [Key Concepts](#key-concepts)
- [Example Usage](#example-usage)
- [Coding Standards](#coding-standards)
- [Testing](#testing)
- [Contributing](#contributing)
- [License](#license)

## Project Description

The `Routing.Abstractions` package defines the contract and abstraction layer for the DroidNet routing system. It contains interfaces for routers, routes, navigation contexts, and related components that enable developers to build sophisticated navigation systems in WinUI applications. This abstractions library ensures consistency across different routing implementations and provides a clear contract for extensibility.

## Technology Stack

- **Language**: C# 13 (with nullable reference types enabled)
- **.NET Target Frameworks**:
  - .NET 9.0
  - .NET 9.0-Windows 10.0.26100.0 (Windows-specific APIs)
- **Platform**: WinUI 3 / Windows App SDK 1.8+
- **Key Dependencies**:
  - `Destructurama.Attributed` (v5.1.0): Attribute-driven destructuring for structured logging
  - `System.Reactive` (v6.1.0): Reactive extensions for asynchronous event handling
  - `Microsoft.Extensions.DependencyInjection.Abstractions`: DI container integration

## Architecture

The Routing.Abstractions library follows a layered architecture pattern:

### Core Abstractions Layer

Defines the contracts for the routing system:

- **Router Interface** (`IRouter`): Entry point for navigation operations and state management
- **Route Configuration** (`IRoute`, `IRoutes`): Hierarchical route definitions
- **Navigation Context** (`INavigationContext`): Manages navigation targets and state persistence
- **Route Activation** (`IRouteActivator`, `IRouteActivationObserver`): Pluggable ViewModel instantiation and lifecycle management

### URL Processing Layer

Handles complex URL parsing and serialization:

- **URL Segment Groups** (`IUrlSegmentGroup`, `IUrlSegment`): Parsed URL components
- **URL Tree** (`IUrlTree`): Complete URL structure representation
- **Route Matching** (`IMatchResult`): Pattern matching results and validation

### Router State Layer

Manages the current navigation state:

- **Active Routes** (`IActiveRoute`, `IActiveRouteTreeNode`): Current route activation state
- **Router State** (`IRouterState`): Complete snapshot of router state
- **Parameters** (`IParameters`, `Parameter`): Route parameters and their values

### Event System Layer

Rich event system for monitoring the navigation lifecycle:

- `NavigationStart`, `NavigationEnd`, `NavigationError`
- `ActivationStarted`, `ActivationComplete`
- `ContextCreated`, `ContextChanged`, `ContextDestroyed`
- `RouterReady`, `RoutesRecognized`

## Getting Started

### Installation

This package is part of the DroidNet routing ecosystem. Install it via NuGet:

```bash
dotnet add package DroidNet.Routing.Abstractions
```

Or reference it in your `.csproj`:

```xml
<ItemGroup>
    <PackageReference Include="DroidNet.Routing.Abstractions" />
</ItemGroup>
```

The version is centrally managed in `Directory.Packages.props`.

### Basic Setup

The abstractions library is typically used through the concrete implementation in `Routing.Router` or `Routing.WinUI`. Here's a typical usage pattern:

```csharp
// Define routes using the abstraction contracts
IRoutes routes = new Routes(new[]
{
    new Route
    {
        Path = "home",
        ViewModelType = typeof(HomeViewModel),
        Outlet = OutletName.Primary
    },
    new Route
    {
        Path = "about",
        ViewModelType = typeof(AboutViewModel),
        Outlet = OutletName.Primary
    }
});

// Use the router for navigation
await router.NavigateByUrl("/home");
```

### Prerequisites

- .NET 9.0 SDK or later
- WinUI 3 capable Windows development environment (Windows 10 Build 26100 or later)
- Visual Studio 2022 17.12 or later (recommended)

## Project Structure

```text
src/
  ├── Routing.Abstractions.csproj        # Project file
  ├── IRouter.cs                         # Main router interface
  ├── IRoute.cs                          # Route configuration
  ├── IRoutes.cs                         # Route collection
  ├── INavigationContext.cs              # Navigation context management
  ├── IRouterState.cs                    # Router state snapshot
  ├── IActiveRoute.cs                    # Active route tracking
  ├── IParameters.cs                     # Parameter handling
  ├── IUrlTree.cs                        # URL parsing
  ├── IRouteActivator.cs                 # ViewModel/view instantiation
  ├── IRoutingAware.cs                   # Component routing hooks
  ├── Parameter.cs                       # Parameter model
  ├── NavigationOptions.cs               # Navigation configuration
  ├── Events/                            # Navigation event types
  │   ├── NavigationStart.cs
  │   ├── NavigationEnd.cs
  │   ├── NavigationError.cs
  │   ├── ActivationStarted.cs
  │   ├── ActivationComplete.cs
  │   └── ...
  ├── OutletName.cs                      # Named outlet constants
  ├── Target.cs                          # Navigation target
  ├── FullNavigation.cs                  # Full navigation descriptor
  ├── PartialNavigation.cs               # Child route navigation
  └── *.cs                               # Additional exception and model classes

Properties/
  └── GlobalSuppressions.cs              # Global code analysis suppressions
```

## Core Features

- **URL-Based Navigation**: Navigate between views using URL patterns with support for complex path structures
- **Multiple Outlet Support**: Place views in named outlets (primary, modal, popup, etc.) for advanced layout control
- **Matrix Parameters**: Support for URL matrix parameters (e.g., `/documentation/page;lang=en;format=pdf`)
- **Query Parameters**: Full query parameter handling with multi-value support
- **Hierarchical Routes**: Define nested route structures mirroring application hierarchy
- **Dynamic State Management**: Manipulate router state programmatically for complex navigation scenarios
- **Route Pattern Matching**: Flexible path matching with prefix and exact match support
- **Navigation Lifecycle Events**: Rich event system with detailed navigation phase tracking
- **Pluggable Route Activation**: Extensible ViewModel/view instantiation through `IRouteActivator`
- **Parameter Handling**: Comprehensive parameter binding and retrieval from routes

## Key Concepts

### Routes

Hierarchical route definitions that map URL paths to ViewModels. Routes form a tree structure that mirrors your application's navigation hierarchy.

```csharp
var routes = new Routes(new[]
{
    new Route
    {
        Path = "projects",
        ViewModelType = typeof(ProjectsViewModel),
        Children = new Routes(new[]
        {
            new Route
            {
                Path = "details",
                ViewModelType = typeof(ProjectDetailsViewModel),
                Outlet = "main"
            },
            new Route
            {
                Path = "info",
                ViewModelType = typeof(ProjectInfoViewModel),
                Outlet = "side"
            }
        })
    }
});
```

### Router State

Represents the current application state as a tree of activated routes and their associated ViewModels. Enables inspection of the current navigation state and provides context for components.

```csharp
IRouterState state = router.RouterState;
IActiveRoute activeRoute = state.Root;
```

### Navigation Context

Manages where views are rendered (windows, panels, etc.) and maintains router state for that context. Enables multiple independent navigation contexts within an application.

### Route Activation

Pluggable system for instantiating and initializing ViewModels when routes are activated. Implement `IRouteActivator` to customize this behavior.

### Navigation Events

Rich event system for monitoring and controlling the navigation lifecycle:

- `NavigationStart`: Navigation initiated
- `RoutesRecognized`: URL parsed and matched to routes
- `ActivationStarted`: ViewModel instantiation begins
- `ActivationComplete`: ViewModel ready
- `NavigationEnd`: Navigation complete
- `NavigationError`: Navigation error occurred

## Example Usage

### Route Configuration

```csharp
var routes = new Routes(new[]
{
    new Route
    {
        Path = "home",
        ViewModelType = typeof(HomeViewModel),
        Outlet = OutletName.Primary
    },
    new Route
    {
        Path = "dashboard",
        ViewModelType = typeof(DashboardViewModel),
        Outlet = OutletName.Primary,
        Children = new Routes(new[]
        {
            new Route
            {
                Path = "widgets",
                ViewModelType = typeof(WidgetsViewModel),
                Outlet = "widgets-panel"
            },
            new Route
            {
                Path = "settings",
                ViewModelType = typeof(SettingsViewModel),
                Outlet = "settings-panel"
            }
        })
    }
});
```

### Navigation with Parameters

```csharp
// Navigate with matrix parameters
await router.NavigateByUrl("/projects/details;projectId=123");

// Navigate with query parameters
await router.NavigateByUrl("/search?query=test&sort=date");

// Navigate with outlet syntax
await router.NavigateByUrl("/dashboard(widgets:panels;detail=expanded)");
```

### Subscribing to Navigation Events

```csharp
router.Events.Subscribe(evt =>
{
    if (evt is NavigationStart start)
    {
        Debug.WriteLine($"Navigation starting: {start.Url}");
    }
    else if (evt is NavigationEnd end)
    {
        Debug.WriteLine("Navigation complete");
    }
});
```

## Coding Standards

This project follows the DroidNet repository C# coding standards:

- **C# Version**: C# 13 (with preview features enabled)
- **Nullable Reference Types**: Enabled (`<nullable>enable</nullable>`)
- **Implicit Usings**: Enabled (`<ImplicitUsings>enable</ImplicitUsings>`)
- **Access Modifiers**: Always explicit (`public`, `private`, `protected`, `internal`)
- **Instance Member References**: Explicit `this.` qualifier for member access
- **Naming Conventions**: PascalCase for public members, camelCase for locals
- **Code Analysis**: Strict code analysis enabled with StyleCop.Analyzers and Roslynator

See `.github/instructions/csharp_coding_style.instructions.md` for detailed style requirements.

## Testing

The abstractions library defines contracts that are tested through integration tests in the concrete implementations (`Routing.Router.Tests`, `Routing.WinUI.Tests`).

### Test Framework

- **Framework**: MSTest 4.0
- **Pattern**: AAA (Arrange-Act-Assert)
- **Naming**: `MethodName_Scenario_ExpectedBehavior`
- **Assertion Library**: AwesomeAssertions

### Running Tests

```bash
# Run all routing-related tests
dotnet test projects/Routing/Routing.Router/tests/Routing.Router.Tests/Routing.Router.Tests.csproj

# Run with coverage
dotnet test projects/Routing/Routing.Router/tests/Routing.Router.Tests/Routing.Router.Tests.csproj `
    /p:CollectCoverage=true /p:CoverletOutputFormat=lcov
```

See `.github/prompts/csharp-mstest.prompt.md` for MSTest conventions.

## Contributing

When working with this library:

1. **Maintain Abstraction Purity**: Ensure interfaces remain implementation-agnostic
2. **Document Contracts**: All interface members should have clear XML documentation
3. **Follow Conventions**: Use existing patterns and naming conventions
4. **Test Coverage**: Add tests for new abstractions in appropriate test projects
5. **Backward Compatibility**: Be cautious when modifying existing interfaces as they're consumed by other packages

For general contribution guidelines, see the main repository's contributing documentation and the `.github/copilot-instructions.md` file.

## License

This project is part of the DroidNet repository. See the LICENSE file in the repository root for details.
