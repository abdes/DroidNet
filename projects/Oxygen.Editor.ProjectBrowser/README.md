# Oxygen.Editor.ProjectBrowser

A WinUI 3 library that provides a comprehensive project browser interface for the Oxygen Editor, enabling users to browse, create, and manage projects through an intuitive start screen with template support.

## Overview

The Oxygen.Editor.ProjectBrowser module is part of the [DroidNet](https://github.com/abdes/DroidNet) repository and serves as the project management entry point for the Oxygen Editor. It provides an intuitive user interface for discovering, creating, and opening game development projects with support for extensible project templates.

## Technology Stack

| Technology | Version | Purpose |
|-----------|---------|---------|
| **C#** | 13 (preview) | Primary language with nullable reference types and implicit usings |
| **WinUI 3** | Microsoft.WindowsAppSDK 1.8+ | Windows UI framework for modern desktop applications |
| **.NET** | 9.0 (net9.0-windows10.0.26100.0) | Target framework |
| **CommunityToolkit.Mvvm** | Latest | MVVM pattern implementation and source generators |
| **CommunityToolkit.WinUI** | Latest | WinUI extensions and utilities |
| **System.Reactive** | Latest | Reactive programming support |
| **DryIoc** | 6.0+ | Dependency injection container |
| **Testably.Abstractions** | Latest | File system abstraction for testing |

## Project Architecture

The project is organized using a layered MVVM architecture:

```text
Oxygen.Editor.ProjectBrowser/
├── src/
│   ├── Controls/           # Reusable WinUI controls
│   ├── Projects/           # Project management interfaces and services
│   ├── Templates/          # Template management interfaces and services
│   ├── ViewModels/         # MVVM ViewModels for different screens
│   ├── Views/              # XAML views paired with code-behind
│   ├── Themes/             # XAML theme resources
│   └── Strings/            # Localization resources
└── tests/
    └── Unit tests for template loading and services
```

**Key Design Patterns:**

- **MVVM Pattern**: Separates UI logic (Views) from business logic (ViewModels)
- **Dependency Injection**: Services are injected via the DryIoc container
- **Template Strategy Pattern**: Multiple template sources (local and universal) implement `ITemplatesSource`
- **Observable Collections**: Uses Reactive Extensions for real-time updates
- **Settings Persistence**: Uses source-generated descriptors for automatic settings management

## Getting Started

### Prerequisites

- .NET 9.0 SDK or later
- Windows 10/11 with Windows App SDK support
- Visual Studio 2022 or later (recommended)

### Installation

1. **Clone the repository:**

   ```powershell
   git clone https://github.com/abdes/DroidNet.git
   cd DroidNet
   ```

2. **Install dependencies:**

   ```powershell
   pwsh ./init.ps1
   ```

   This script sets up the .NET SDK, restores tools, and configures pre-commit hooks.

3. **Build the project:**

   ```powershell
   dotnet build projects/Oxygen.Editor.ProjectBrowser/src/Oxygen.Editor.ProjectBrowser.csproj
   ```

### Configuration

The project browser requires configuration in the application settings. Add the following to your `appsettings.json`:

```json
{
  "ProjectTemplatesSettings": {
    "Categories": [
      {
        "Name": "GamesCategory",
        "Description": "GamesCategoryDescription",
        "Location": "Games",
        "IsBuiltIn": true
      },
      {
        "Name": "VisualizationCategory",
        "Description": "VisualizationCategoryDescription",
        "Location": "Visualization",
        "IsBuiltIn": true
      },
      {
        "Name": "ExtendedTemplatesCategory",
        "Description": "ExtendedTemplatesCategoryDescription",
        "Location": "Templates",
        "IsBuiltIn": false
      }
    ]
  }
}
```

Register the services in your DI container:

```csharp
services.Configure<ProjectBrowserSettings>(configuration.GetSection("ProjectBrowserSettings"));
services.AddSingleton<IProjectBrowserService, ProjectBrowserService>();
services.AddSingleton<ITemplatesService, TemplatesService>();
```

## Project Structure

### Folder Organization

```plaintext
Oxygen.Editor.ProjectBrowser/
├── src/
│   ├── Assets/             # Built-in project templates organized by category
│   ├── Controls/           # Reusable WinUI controls
│   ├── Properties/         # Project properties and assembly metadata
│   ├── Projects/           # Project management interfaces and services
│   ├── Strings/            # Localization resources (en-US)
│   ├── Templates/          # Template management interfaces and services
│   ├── Themes/             # XAML theme resources
│   ├── ViewModels/         # MVVM ViewModels for different screens
│   ├── Views/              # XAML views paired with code-behind
│   ├── ProjectBrowserSettings.cs  # Configuration model
│   └── Oxygen.Editor.ProjectBrowser.csproj
└── tests/
    ├── LocalTemplatesSourceTests.cs  # Template loading tests
    └── OxygenEditor.ProjectBrowser.Tests.csproj
```

### Detailed Component Breakdown

#### Controls

- **KnownLocationsListView** - Lists known project storage locations for quick access
- **RecentProjectsList** - Displays recently opened projects with quick open functionality
- **TemplatesGridView** - Grid view for browsing available project templates by category

#### Services

**IProjectBrowserService** - Central service for project management:

- `GetRecentlyUsedProjectsAsync()` - Retrieve recently opened projects
- `CanCreateProjectAsync()` - Validate project creation
- `NewProjectFromTemplate()` - Create project from template
- `OpenProjectAsync()` - Open existing project
- `GetQuickSaveLocations()` - Get common save locations (Recent, Desktop, Documents, etc.)
- `GetKnownLocationsAsync()` - Get file system locations for browsing

**ITemplatesService** - Template discovery and management:

- `GetLocalTemplatesAsync()` - Enumerate available templates from local sources
- `HasRecentlyUsedTemplatesAsync()` - Check if there is template history
- `GetRecentlyUsedTemplates()` - Observable stream of recently used templates

#### ViewModels

- **MainViewModel** - Orchestrates navigation between start screen tabs (Home, New, Open)
- **HomeViewModel** - Displays recent projects and frequently used templates
- **NewProjectViewModel** - Manages project creation workflow and template selection
- **OpenProjectViewModel** - Handles project browsing and selection for opening existing projects

#### Views

- **MainView.xaml** - Root start screen container with tab navigation
- **HomeView.xaml** - Home tab showing recent projects and templates
- **NewProjectView.xaml** - Create new project interface with template selection
- **NewProjectDialog.xaml** - Modal dialog for project creation confirmation
- **OpenProjectView.xaml** - Browse and open existing projects with file system navigation

#### Assets

Built-in project templates organized by category (copied to application data folder):

- **Games** - Game development templates:
  - Blank game project
  - First-Person game starter
- **Visualization** - Visualization project templates:
  - Blank visualization project
- **Custom** - Extensible location for user-provided templates

## Key Features

- **Project Browsing** - Effortlessly browse recently used projects and known file system locations
- **Project Creation** - Create new projects from built-in or custom templates with validation
- **Template Management** - Extensible template system supporting multiple sources (local, universal)
- **Quick Save Locations** - Convenient shortcuts to common save directories (Recent, Desktop, Documents, Downloads, OneDrive)
- **Project Validation** - Pre-creation validation to ensure project viability before instantiation
- **Settings Persistence** - Automatically save window state, user preferences, and recent project list
- **Localization Ready** - Full localization support with string resources in `Strings/en-US/`
- **Observable Patterns** - Reactive programming support for real-time UI updates using System.Reactive

## Development Workflow

### Building

```powershell
# Build the ProjectBrowser module specifically
dotnet build projects/Oxygen.Editor.ProjectBrowser/src/Oxygen.Editor.ProjectBrowser.csproj

# Build all projects (from projects/ directory)
cd projects
.\open.cmd
```

### Testing

```powershell
# Run all ProjectBrowser tests
dotnet test projects/Oxygen.Editor.ProjectBrowser/tests/OxygenEditor.ProjectBrowser.Tests.csproj

# Run with coverage
dotnet test projects/Oxygen.Editor.ProjectBrowser/tests/OxygenEditor.ProjectBrowser.Tests.csproj `
  /p:CollectCoverage=true /p:CoverletOutputFormat=lcov
```

### Clean Build

```powershell
# Clean all artifacts
.\clean.ps1
```

## Coding Standards

This project follows the DroidNet repository C# coding style:

- **Explicit Access Modifiers** - All types and members must have explicit `public`, `private`, `protected`, or `internal` modifiers
- **This Reference** - Always use `this.` when referencing instance members
- **Braces** - Always use braces `{}` for control statements, even for single-line statements
- **Type Inference** - Use `var` for obvious types; otherwise use explicit types
- **Expression-Bodied Members** - Use for simple properties and methods where appropriate
- **Nullable Reference Types** - Enabled; follow strict nullability analysis
- **C# 13 Features** - Preview features are enabled; use modern language constructs

See `.github/instructions/csharp_coding_style.instructions.md` for detailed guidelines.

### MVVM Patterns

The project uses source-generated MVVM wiring from the [Mvvm.Generators](../Mvvm.Generators/) module:

- Use `[ObservableProperty]` from CommunityToolkit.Mvvm for properties that notify on change
- Inherit from `ObservableObject` for all ViewModels
- Use `[RelayCommand]` for command properties wired to view interactions
- Apply `[ViewModel(typeof(TViewModel))]` attribute on View classes for automatic code-generation of binding wiring
- Implement `IRoutingAware` for ViewModels that participate in navigation workflows
- Prefer XAML data binding over code-behind logic for UI state management

## Testing

Tests use **MSTest** framework following DroidNet conventions:

- **Test Project**: `OxygenEditor.ProjectBrowser.Tests`
- **Naming Convention**: `MethodName_Scenario_ExpectedBehavior`
- **Test Pattern**: Arrange-Act-Assert (AAA) pattern
- **Parameterized Tests**: Use `[DataRow]` and `[DataTestMethod]` for multiple test cases
- **Mocking**: Use `Moq` for service mocking and `Testably.Abstractions` for file system mocking

### Example Test Structure

```csharp
[TestClass]
public class LocalTemplatesSourceTests
{
    [TestMethod]
    [DataRow("ValidPath")]
    public async Task GetTemplates_WithValidDirectory_ReturnsTemplates(string path)
    {
        // Arrange
        var source = new LocalTemplatesSource(path);

        // Act
        var templates = await source.GetTemplates().ToListAsync();

        // Assert
        Assert.IsNotNull(templates);
        Assert.IsTrue(templates.Count > 0);
    }
}
```

### Test Coverage

Current test coverage focuses on:

- Template source loading and discovery
- Project browser service functionality
- Configuration validation
- File system operations

## Contributing

Contributions to the Oxygen.Editor.ProjectBrowser are welcome! When contributing:

1. **Follow the coding standards** - Review and adhere to `.github/instructions/csharp_coding_style.instructions.md`
   - Use explicit access modifiers (`public`, `private`, etc.)
   - Always reference instance members with `this.`
   - Enable nullable reference types and strict analysis
   - Use C# 13 preview features

2. **Maintain MVVM separation** - Keep UI logic in ViewModels, not in code-behind
   - Use `[ObservableProperty]` and `[RelayCommand]` attributes
   - Prefer data binding over imperative code-behind

3. **Add unit tests** - New features and bug fixes should include corresponding tests
   - Follow the AAA (Arrange-Act-Assert) pattern
   - Use MSTest with proper naming conventions

4. **Update documentation** - Keep this README and any design documentation current
   - Document public APIs and their usage
   - Update the [src/README.md](src/README.md) for implementation details

5. **Make small, focused changes** - Prefer small surface-area modifications
   - One feature or fix per pull request
   - Keep commits atomic and well-described

6. **Check existing patterns** - Review existing code before proposing new patterns
   - Look at similar implementations in the codebase
   - Follow established conventions for consistency

## Related Projects

- [Oxygen.Editor](../Oxygen.Editor/) - Main editor application
- [Oxygen.Core](../Oxygen.Core/) - Core editor services
- [Oxygen.Editor.ProjectBrowser](../Oxygen.Editor.ProjectBrowser/) - Project browsing interface (this module)
- [Mvvm.Generators](../Mvvm.Generators/) - Source generators for MVVM wiring

## Resources

- **[Oxygen Editor Documentation](../Oxygen.Editor/)** - Main editor project overview
- **[DroidNet Repository](https://github.com/abdes/DroidNet)** - Full mono-repo with all projects
- **[Project Browser Configuration](src/README.md)** - Detailed configuration and services documentation
- **[WinUI 3 Documentation](https://learn.microsoft.com/en-us/windows/apps/winui/)** - Official WinUI 3 guides and API reference
- **[MVVM Toolkit](https://learn.microsoft.com/en-us/windows/communitytoolkit/mvvm/)** - CommunityToolkit.Mvvm documentation
- **[C# Coding Style](../../.github/instructions/csharp_coding_style.instructions.md)** - DroidNet C# conventions
- **[MSTest Framework](https://learn.microsoft.com/en-us/dotnet/core/testing/unit-testing-with-mstest)** - MSTest documentation and examples

## License

Distributed under the MIT License. See [LICENSE](../../LICENSE) file for details.
