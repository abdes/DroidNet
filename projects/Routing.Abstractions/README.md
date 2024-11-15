# Router for WinUI navigation - Abstractions

DroidNet.Routing is a .NET 8.0 library that implements an Angular-inspired router for WinUI applications. It enables URL-based navigation and dynamic view management within Windows desktop applications.

## Core Features

- **URL-Based Navigation**: Navigate between views using URL patterns, similar to web applications
- **Multiple Navigation Contexts**: Support for multiple windows and nested navigation contexts
- **Dynamic State Management**: Manipulate router state programmatically for complex navigation scenarios
- **Outlet System**: Place views in named outlets for advanced layout control, including master-detail and side panel scenarios
- **Parameter Handling**: Support for URL matrix parameters and query parameters with multi-value capabilities

## Key Concepts

- **Routes**: Configure application navigation structure using a hierarchical route configuration that maps URLs to ViewModels
- **Router State**: Represents the current application state as a tree of activated routes and their associated ViewModels
- **Navigation Context**: Manages where views are rendered (windows, panels, etc.) and maintains router state
- **Route Activation**: Handles ViewModel creation and view loading through a pluggable activation system
- **Navigation Events**: Rich event system for monitoring and controlling the navigation lifecycle

## Example Route Configuration
```cs
var routes = new Routes([
    new Route {
        Path = "projects",
        ViewModelType = typeof(ProjectViewModel),
        Children = new Routes([
            new Route {
                Path = "assets",
                ViewModelType = typeof(AssetsViewModel),
                Outlet = "main"
            },
            new Route {
                Path = "info",
                ViewModelType = typeof(InfoViewModel),
                Outlet = "side"
            }
        ])
    }
]);
```
