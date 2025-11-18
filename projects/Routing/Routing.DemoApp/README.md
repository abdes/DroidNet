# Routing Demo Application

A comprehensive demonstration application showcasing the DroidNet Routing framework—an Angular-inspired URL-based navigation system for WinUI 3 applications. This demo illustrates key navigation patterns including outlets, matrix parameters, nested routes, and dynamic page transitions.

## Technology Stack

- **Language:** C# 13 (preview)
- **UI Framework:** WinUI 3 with XAML
- **Platform:** .NET 10 / Windows 10.0.26100.0
- **Architecture Pattern:** MVVM with source-generated View-ViewModel binding
- **Routing:** DroidNet Routing framework with outlet-based navigation
- **Dependency Injection:** DryIoc with Microsoft.Extensions.DependencyInjection
- **MVVM Toolkit:** CommunityToolkit.Mvvm 8.4
- **Logging:** Serilog with Console and Debug sinks
- **Hosting:** .NET Generic Host with UserInterfaceHostedService

## Project Overview

The Routing Demo Application demonstrates practical implementation of URL-based navigation in a WinUI 3 desktop application. It serves as a working reference for:

- Setting up the DroidNet routing framework in a WinUI application
- Implementing MVVM-based navigation views and view models
- Using source-generated view-model binding via the `[ViewModel]` attribute
- Managing multiple navigation outlets and nested routes
- Integrating with the .NET Generic Host for service configuration

## Project Structure

```text
Routing.DemoApp/
├── src/
│   ├── Program.cs                  # Application entry point with Host configuration
│   ├── App.xaml / App.xaml.cs     # Application initialization and setup
│   ├── Shell/                      # Main window shell container
│   │   ├── ShellView.xaml
│   │   ├── ShellView.xaml.cs
│   │   └── ShellViewModel.cs       # Primary outlet container
│   ├── Navigation/                 # Navigation pages and view models
│   │   ├── RoutedNavigationView.xaml
│   │   ├── PageOnePage.xaml & PageOneViewModel.cs
│   │   ├── PageTwoView.xaml & PageTwoViewModel.cs
│   │   ├── PageThree.xaml & PageThreeViewModel.cs
│   │   ├── SettingsView.xaml & SettingsViewModel.cs
│   │   └── NavigationItem.cs       # Navigation item model
│   ├── Styles/                     # XAML resource dictionaries
│   │   ├── FontSizes.xaml
│   │   ├── TextBlock.xaml
│   │   └── Thickness.xaml
│   ├── Properties/
│   │   └── AssemblyInfo.cs
│   └── Routing.Demo.App.csproj    # Project file
├── Assets/                         # Application icons and resources
└── bin / artifacts/               # Build outputs
```

## Getting Started

### Prerequisites

- [.NET 10 SDK](https://dotnet.microsoft.com/download)
- Visual Studio 2022 with WinUI 3 support, or VS Code with C# extension
- Windows 10/11 with Windows App SDK 1.8+

### Building the Application

To build the demo application, use:

```powershell
dotnet build projects/Routing/Routing.DemoApp/src/Routing.Demo.App.csproj
```

Or build from the Routing solution:

```powershell
cd projects/Routing
dotnet build
```

### Running the Application

Execute the built application:

```powershell
dotnet run --project projects/Routing/Routing.DemoApp/src/Routing.Demo.App.csproj
```

Or run directly from the artifacts directory after building.

## Key Features

### 1. **Navigation-Based UI**

The demo implements URL-based routing where each page corresponds to a route, enabling:

- Direct navigation via URL paths (e.g., `/1`, `/2`, `/settings`)
- Browser-like back/forward navigation patterns
- Deep linking to specific application states

### 2. **Multiple Navigation Pages**

Three main content pages (PageOne, PageTwo, PageThree) plus a Settings page:

- Each page is a XAML view with an associated ViewModel
- Pages implement `IRoutingAware` to react to navigation events
- Demonstrates relative navigation between siblings

### 3. **Outlet-Based Layout**

The main window uses a primary outlet to host content:

- The `ShellView` acts as the main container
- The `ShellViewModel` manages the primary outlet
- Content ViewModels are dynamically loaded/unloaded based on routes

### 4. **View-ViewModel Binding**

Source-generated binding via the `[ViewModel]` attribute:

- Views automatically wire to their ViewModels without code-behind
- Minimal XAML code-behind—only event subscriptions and UI setup
- Type-safe ViewModel references

### 5. **Navigation Items**

A `NavigationItem` model encapsulates route information:

- Navigation path
- Display text and icon
- Keyboard access key
- Target ViewModel type

## Architecture & Patterns

### Host & Dependency Injection

The application uses the .NET Generic Host for:

- Service configuration and registration (Serilog, DryIoc, Routing, Aura)
- Lifetime management
- UI hosting via `UserInterfaceHostedService`

Key startup configuration in `Program.cs`:

```csharp
var host = Host.CreateDefaultBuilder()
    .ConfigureServices(services => {
        // DI configuration for routing, UI, logging, etc.
    })
    .ConfigureUserInterfaceHosting(context => {
        // WinUI 3 hosting configuration
    })
    .UseSerilog()
    .Build();

await host.RunAsync();
```

### MVVM & Routing Integration

**ViewModels** inherit from or implement routing-aware interfaces:

- Implement `IRoutingAware` to receive navigation lifecycle events (`OnNavigatedToAsync`)
- Access the current route via `IActiveRoute` parameter
- Support relative navigation via `IRouter.NavigateAsync()`

**Views** use the `[ViewModel(typeof(TViewModel))]` attribute for source-generated binding, eliminating boilerplate and ensuring type safety.

### Navigation Flow

1. User clicks a navigation item (e.g., "Page 2")
2. The router updates the URL (e.g., `/2`)
3. The route is matched against registered routes
4. The corresponding ViewModel is instantiated and injected
5. `OnNavigatedToAsync` is called on the ViewModel
6. The view's outlet is updated with the new content

## Development Workflow

### Building & Testing

- **Build the project:**

  ```powershell
  dotnet build projects/Routing/Routing.DemoApp/src/Routing.Demo.App.csproj
  ```

- **Regenerate the Routing solution** (if modifying multiple projects):

  ```powershell
  cd projects/Routing
  dotnet slngen -d . -o Routing.sln --folders false .\**\*.csproj
  ```

### Adding New Pages

To add a new navigation page:

1. Create a new XAML View and ViewModel in the `Navigation/` folder
2. Implement `IRoutingAware` in the ViewModel
3. Register the route in the application startup (Program.cs or Startup.cs)
4. Add a `NavigationItem` for the new route
5. Apply the `[ViewModel(typeof(NewPageViewModel))]` attribute to the View

Example ViewModel:

```csharp
public partial class NewPageViewModel(IRouter router) : IRoutingAware
{
    public Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        // Handle navigation to this page
        return Task.CompletedTask;
    }
}
```

### Code Style & Conventions

This project follows the repository's C# style guidelines:

- Explicit access modifiers (`public`, `private`, etc.)
- `this.` prefix for instance members
- Nullable reference types enabled
- Implicit usings (C# 10+)
- StyleCop and Roslynator analyzers enabled

See [C# Coding Style Guide](./../../../.github/instructions/csharp_coding_style.instructions.md) for full conventions.

## Testing

The demo includes no formal tests; it is an executable reference implementation. For testing the Routing framework itself, see `projects/Routing/Routing.Router` or other test projects in the Routing solution.

## Dependencies & Project References

The application references several DroidNet modules:

- **Routing.WinUI** — WinUI 3 integration for the routing framework
- **Hosting** — .NET Generic Host and UserInterfaceHostedService
- **Aura** — Window decoration and theming
- **Bootstrap** — Application bootstrapping utilities
- **Converters** — Value converters for XAML binding
- **Mvvm.Generators** — Source generators for View-ViewModel binding (analyzer reference)

## License

This project is part of the DroidNet repository and is distributed under the **MIT License**. See the root LICENSE file for details.

## See Also

- [DroidNet Routing Framework Documentation](../README.md)
- [WinUI 3 Documentation](https://docs.microsoft.com/en-us/windows/apps/winui/winui3/)
- [.NET Generic Host](https://docs.microsoft.com/en-us/dotnet/core/extensions/generic-host)
- [CommunityToolkit.Mvvm](https://github.com/CommunityToolkit/dotnet)
