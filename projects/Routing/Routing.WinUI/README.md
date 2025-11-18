# Routing.WinUI

A WinUI 3 implementation of routed navigation for building modern Windows applications with MVVM patterns. This module provides a flexible outlet-based navigation system that integrates with the DroidNet routing framework.

## Project Description

`Routing.WinUI` enables sophisticated, Angular-inspired routing in WinUI 3 applications. It implements a view model-driven navigation pattern where routes are resolved to view models, which are then displayed in designated UI regions called **outlets**. The system supports multiple simultaneous outlets, automatic view resolution, and comprehensive error handling for development.

## Technology Stack

- **Target Framework:** .NET 9.0 (Windows 10.0.26100.0)
- **UI Framework:** [WinUI 3](https://learn.microsoft.com/windows/apps/winui/winui3/)
- **Architecture:** MVVM (Model-View-ViewModel)
- **Container:** [DryIoc](https://github.com/dadhi/DryIoc) 6.0+
- **MVVM Toolkit:** [CommunityToolkit.Mvvm](https://github.com/CommunityToolkit/dotnet) 8.4+
- **Language:** C# 13 (preview features)

### Key Dependencies

- `Microsoft.WindowsAppSDK` - WinUI 3 and Windows App SDK
- `Microsoft.Xaml.Behaviors.WinUI.Managed` - Behavior support for WinUI
- `DroidNet.Routing.Router` - Core routing engine
- `DroidNet.Mvvm` - MVVM base classes and converters

## Project Architecture

### Core Components

#### RouterOutlet Control

A user control that represents a designated region in the UI where routed content is dynamically loaded. Key responsibilities:

- Automatically resolves view models to views using a configured `IValueConverter`
- Manages content lifecycle when the ViewModel changes
- Provides visual states (Inactive, Normal, Error) for feedback
- Handles view resolution failures gracefully with error templates

#### AbstractOutletContainer

A base class for view models that host content in multiple outlets:

- Manages outlet registration and content loading
- Provides property change notifications through `INotifyPropertyChanged`
- Simplifies implementation by handling lifecycle and validation
- Maps outlet names to view model properties for binding

#### Integration Services

- `WindowContextProvider` - Manages window-specific navigation context
- `WindowRouteActivator` - Activates routes within windowed contexts
- `HostBuilderExtensions` - Registers routing services in DryIoc container

### Navigation Flow

1. Route is activated by the router
2. Associated view model is instantiated
3. View model is loaded into appropriate outlet(s)
4. `IValueConverter` resolves the view model to its corresponding view
5. View is displayed in the outlet with appropriate visual state

## Getting Started

### Prerequisites

- .NET 9.0 SDK or later
- Windows 10 Build 22000 or later
- Visual Studio 2022 (recommended)

### Installation

Add the NuGet package to your WinUI 3 project:

```bash
dotnet add package DroidNet.Routing.WinUI
```

### Basic Setup

1. **Register the routing system** in your application host:

    ```csharp
    Host.CreateDefaultBuilder()
        .ConfigureServices((context, services) =>
        {
            // Register your views and view models
            services.AddTransient<ShellView>();
            services.AddTransient<ShellViewModel>();
            services.AddTransient<DashboardView>();
            services.AddTransient<DashboardViewModel>();
        })
        .ConfigureRouter(routes =>
        {
            routes.Add(new Route
            {
                Path = string.Empty,
                ViewModelType = typeof(ShellViewModel),
                Children = new Routes([
                    new Route
                    {
                        Path = "dashboard",
                        ViewModelType = typeof(DashboardViewModel)
                    }
                ])
            });
        })
        .Build();
    ```

2. **Add a ViewModel to View converter** to your application resources:

    ```xaml
    <Application.Resources>
        <converters:ViewModelToView x:Key="VmToViewConverter"/>
    </Application.Resources>
    ```

3. **Create an outlet container ViewModel**:

    ```csharp
    public class WorkspaceViewModel : AbstractOutletContainer
    {
        public WorkspaceViewModel()
        {
            // Register outlets with their corresponding property names
            this.Outlets.Add("primary", (nameof(this.MainContent), null));
            this.Outlets.Add("sidebar", (nameof(this.SidebarContent), null));
        }

        public object? MainContent => this.Outlets["primary"].viewModel;
        public object? SidebarContent => this.Outlets["sidebar"].viewModel;
    }
    ```

4. **Add RouterOutlet controls** to your XAML:

    ```xaml
    <Grid>
        <Grid.ColumnDefinitions>
            <ColumnDefinition Width="250"/>
            <ColumnDefinition Width="*"/>
        </Grid.ColumnDefinitions>

        <!-- Sidebar outlet -->
        <local:RouterOutlet
            Grid.Column="0"
            ViewModel="{Binding SidebarContent}"
            VmToViewConverter="{StaticResource VmToViewConverter}"/>

        <!-- Main content outlet -->
        <local:RouterOutlet
            Grid.Column="1"
            ViewModel="{Binding MainContent}"
            VmToViewConverter="{StaticResource VmToViewConverter}"/>
    </Grid>
    ```

## Project Structure

```text
Routing.WinUI/
├── src/                          # Source code
│   ├── AbstractOutletContainer.cs    # Base class for outlet container view models
│   ├── IOutletContainer.cs           # Contract for outlet containers
│   ├── RouterOutlet.cs              # Main outlet control
│   ├── RouterOutlet.xaml            # Outlet UI templates and styles
│   ├── WindowContextProvider.cs      # Window-specific navigation context
│   ├── WindowNavigationContext.cs    # Navigation context data
│   ├── WindowRouteActivator.cs       # Route activation in windows
│   ├── HostBuilderExtensions.cs      # DI container integration
│   ├── Routing.WinUI.csproj          # Project file
│   └── Themes/
│       └── Generic.xaml              # Default control templates
├── tests/                        # UI and unit tests
│   ├── RouterOutletTests.cs          # RouterOutlet control tests
│   ├── App.xaml                      # Test application resources
│   └── Routing.WinUI.UI.Tests.csproj # Test project file
└── README.md                     # This file
```

## Key Features

- **Multiple Outlets Support** - Display different content in multiple regions simultaneously
- **Automatic View Resolution** - Convert view models to views using naming conventions or custom converters
- **Visual States** - Built-in feedback states (Inactive, Normal, Error) for development
- **MVVM Pattern** - Integrates seamlessly with view model-based architectures
- **Error Handling** - Comprehensive error templates and diagnostic information
- **Flexible Container** - Support for any ViewModel that implements outlet container pattern
- **DI Integration** - Seamless integration with DryIoc and Microsoft.Extensions.DependencyInjection

## Development Workflow

### Building the Project

Build the specific project:

```powershell
dotnet build projects/Routing/Routing.WinUI/src/Routing.WinUI.csproj
```

Or build the entire routing solution:

```powershell
dotnet build projects/Routing/Routing.sln
```

### Running Tests

Run UI tests:

```powershell
dotnet test projects/Routing/Routing.WinUI/tests/Routing.WinUI.UI.Tests.csproj
```

### Code Style and Standards

This project adheres to the DroidNet C# coding standards:

- **Language Version:** C# 13 (preview features enabled)
- **Null Safety:** Nullable reference types enabled (`nullable: enable`)
- **Access Modifiers:** Always explicit (e.g., `public`, `private`, `protected`)
- **Instance Members:** Always referenced with `this.`
- **Control Statements:** Always use braces, even for single-line statements
- **Implicit Usings:** Enabled for common namespaces

See [C# Coding Style](../../.github/instructions/csharp_coding_style.instructions.md) for comprehensive guidelines.

## Coding Standards

### Key Principles

1. **MVVM Pattern** - Keep code-behind minimal; most logic should reside in ViewModels
2. **Data Binding** - Use data binding for UI updates instead of code-behind manipulation
3. **Theme Awareness** - Leverage WinUI theme resources for colors, fonts, and styles
4. **Explicit Types** - Use explicit access modifiers for all types and members
5. **Code Clarity** - Always use `this.` for instance members and braces for control statements

### View Model Implementation

When creating outlet container view models:

```csharp
public class MyContainerViewModel : AbstractOutletContainer
{
    public MyContainerViewModel()
    {
        // Register all outlets in constructor
        this.Outlets.Add("primary", (nameof(this.PrimaryContent), null));
    }

    // Properties expose outlet content for binding
    public object? PrimaryContent => this.Outlets["primary"].viewModel;

    // Other view model logic...
}
```

## Testing

This project uses **MSTest** framework with the following conventions:

### Test Structure

- **Test Class Naming:** `[ComponentName]Tests`
- **Test Method Naming:** `[MethodName]_[Scenario]_[ExpectedBehavior]`
- **Pattern:** AAA (Arrange-Act-Assert)
- **Categories:** Mark with `[TestCategory]` (e.g., "RouterOutlet", "UITest")

### Example Test

```csharp
[TestClass]
public class RouterOutletTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task VisualState_Normal_WhenViewModelIsSet_Async() => this.EnqueueAsync(
    async () =>
    {
        // Arrange
        var outlet = new RouterOutlet { VmToViewConverter = new TestVmToViewConverter() };
        await this.LoadTestContentAsync(outlet).ConfigureAwait(true);
        var vsm = this.InstallCustomVisualStateManager(outlet);

        // Act
        outlet.ViewModel = new TestViewModel();

        // Assert
        _ = outlet.OutletContent.Should().NotBeNull();
        _ = vsm.GetCurrentStates(outlet).Should().Contain(RouterOutlet.NormalVisualState);
    });
}
```

### Running Tests

```powershell
# Run specific test project
dotnet test projects/Routing/Routing.WinUI/tests/Routing.WinUI.UI.Tests.csproj

# Run with coverage
dotnet test projects/Routing/Routing.WinUI/tests/Routing.WinUI.UI.Tests.csproj /p:CollectCoverage=true
```

## API Examples

### Creating an Outlet Container

```csharp
public class ApplicationShellViewModel : AbstractOutletContainer
{
    public ApplicationShellViewModel()
    {
        // Define outlets for different navigation regions
        this.Outlets.Add("main", (nameof(this.MainContent), null));
        this.Outlets.Add("modal", (nameof(this.ModalContent), null));
        this.Outlets.Add("notification", (nameof(this.NotificationContent), null));
    }

    public object? MainContent => this.Outlets["main"].viewModel;
    public object? ModalContent => this.Outlets["modal"].viewModel;
    public object? NotificationContent => this.Outlets["notification"].viewModel;
}
```

### Using RouterOutlet in XAML

```xaml
<local:RouterOutlet
    ViewModel="{Binding MainContent}"
    VmToViewConverter="{StaticResource VmToViewConverter}"
    Margin="12"/>
```

### Configuring the Router

```csharp
.ConfigureRouter(routes =>
{
    routes.Add(new Route
    {
        Path = string.Empty,
        ViewModelType = typeof(ApplicationShellViewModel),
        Children = new Routes([
            new Route { Path = "home", ViewModelType = typeof(HomeViewModel) },
            new Route { Path = "settings", ViewModelType = typeof(SettingsViewModel) }
        ])
    });
})
```

## Contributing

When contributing to this project:

1. Follow the C# coding standards defined in [Coding Standards](#coding-standards)
2. Write tests for new functionality using MSTest
3. Ensure all tests pass before submitting changes
4. Keep ViewModels focused and separate concerns
5. Use the MVVM pattern consistently
6. Document public APIs with XML comments

For detailed contribution guidelines, see [CONTRIBUTING.md](../../CONTRIBUTING.md).

## License

This project is distributed under the MIT License. See [LICENSE](../../LICENSE) for details.

## Related Projects

- [Routing.Router](../Routing.Router/) - Core routing engine
- [Routing.Abstractions](../Routing.Abstractions/) - Routing interfaces and abstractions
- [Mvvm](../Mvvm/) - MVVM base classes and utilities
- [Docking](../Docking/) - Docking window framework that works with Routing

## Further Reading

- [WinUI 3 Documentation](https://learn.microsoft.com/windows/apps/winui/winui3/)
- [MVVM Pattern Overview](https://learn.microsoft.com/dotnet/architecture/mvvm/)
- [DryIoc Container Documentation](https://github.com/dadhi/DryIoc/wiki)
- [CommunityToolkit.Mvvm](https://github.com/CommunityToolkit/dotnet/tree/main/components/MVVM)
