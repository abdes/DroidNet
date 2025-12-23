# Oxygen.Editor.World

A comprehensive WinUI 3 world editor for the Oxygen game engine, enabling developers to design and manage virtual environments with intuitive tools for terrain sculpting, object placement, lighting configuration, asset management, and real-time playtesting.

## Overview

The Oxygen World Editor is a vital tool in game development, providing developers with an integrated development environment to create and manage game worlds. With this editor, developers can:

- Design and sculpt virtual terrains
- Place, manipulate, and manage game objects
- Adjust lighting and environmental settings
- Import and organize game assets
- Edit entity properties and components
- Manage project files and resources
- Preview changes in real-time
- Optimize performance and manage rendering

## Technology Stack

- **Language:** C# 13 (preview features)
- **Framework:** WinUI 3 with .NET 9
- **Target Framework:** `net9.0-windows10.0.26100.0`
- **Platform:** Windows 10 or later (build 26100.0)
- **Application Pattern:** MVVM with source-generated view-model wiring
- **DI Container:** DryIoc
- **Logging:** Serilog (via parent infrastructure)

### Key Dependencies

- **CommunityToolkit.Mvvm** 8.4 – Provides `ObservableObject` base class and MVVM utilities
- **CommunityToolkit.WinUI.Collections** – Advanced collection controls for WinUI
- **CommunityToolkit.WinUI.Controls.Segmented** – Segmented control components
- **CommunityToolkit.WinUI.Triggers** – Trigger utilities for WinUI
- **Microsoft.WindowsAppSDK** 1.8+ – Core WinUI 3 framework
- **Testably.Abstractions** – File system abstraction for testing
- **DryIoc** 6.0 preview – Dependency injection container
- **Microsoft.Extensions.Logging.Abstractions** – Standard logging abstractions

### Project Dependencies

The WorldEditor integrates with multiple framework modules:

- **Hosting** – Application initialization and service hosting
- **Routing** – URL-based navigation system (Router, Abstractions, WinUI)
- **Docking** – Flexible window docking framework
- **Mvvm** – MVVM infrastructure and utilities
- **Controls** – Custom control libraries (DynamicTree, InPlaceEdit, OutputConsole)
- **Collections** – Advanced collection utilities
- **Converters** – Data conversion helpers
- **Resources** – Resource management system
- **TimeMachine** – Undo/redo functionality
- **Oxygen.Core** – Core editor services
- **Oxygen.Editor.Data** – Data management and persistence
- **Oxygen.Editor.Projects** – Project file handling
- **Oxygen.Storage** – Storage abstraction layer

## Project Architecture

The Oxygen World Editor follows a modular, MVVM-based architecture organized by functional concerns:

```text
src/
├── ContentBrowser/        # Asset browsing and management (grid/list/tiles layouts)
├── Editors/               # Document editing (tabbed interface, logging)
├── Output/                # Output/logging console
├── ProjectExplorer/       # Project file tree navigation
├── PropertiesEditor/      # Entity and component property editing
├── SceneExplorer/         # Scene hierarchy and object management
├── Routing/               # Route definitions and navigation handlers
├── Workspace/             # Workspace state and configuration
├── Themes/                # WinUI theme resources
├── Styles/                # UI styling and control templates
├── Utils/                 # Utility classes and helpers
├── Messages/              # MVVM messaging contracts
├── en-US/                 # Localization resources
└── Constants.cs           # Application constants
```

### Key Components

**ContentBrowser** – Displays and manages game assets with multiple layout options:

- Grid, list, and tile views for browsing assets
- Asset indexing and search capabilities
- Type-based categorization and filtering

**PropertiesEditor** – Inspector-style property editing:

- Component and property visualization
- Multi-selection support with mixed values display
- Expandable property groups and detailed sections

**SceneExplorer** – Hierarchical scene representation:

- Tree-based node organization
- Selection management and synchronization
- Context-aware operations

**Editors** – Document management and editing:

- Tabbed interface for multiple open documents
- Integrated output and logging panels
- Document state tracking

**Workspace** – Configuration and session management:

- Workspace layout and window state persistence
- Docking configuration

## Getting Started

### Prerequisites

- .NET 9 SDK or later
- Visual Studio 2022 or Visual Studio Code with C# extension
- Windows 10 build 26100.0 or later

### Installation

1. Clone the repository:

   ```powershell
   git clone https://github.com/abdes/DroidNet.git
   cd DroidNet
   ```

2. Initialize the workspace (run once after fresh checkout):

   ```powershell
   pwsh ./init.ps1
   ```

3. Build the WorldEditor project:

   ```powershell
   dotnet build projects/Oxygen.Editor.World/src/Oxygen.Editor.World.csproj
   ```

### Running the Application

From the `projects/Oxygen.Editor.World` directory:

```powershell
.\open.cmd
```

Or build and run directly:

```powershell
dotnet run --project src/Oxygen.Editor.World.csproj
```

## Project Structure

### Organization Principles

- **src/** – Main application source code, organized by feature/concern
- **Modular design** – Each functional area (ContentBrowser, Editors, etc.) is self-contained
- **MVVM pattern** – Views and ViewModels are wired via source generators using `[ViewModel(typeof(TViewModel))]` attribute
- **DI-driven** – Services registered through the hosting infrastructure
- **Docking support** – Layout management via the Docking framework for flexible window arrangements

### Source Organization

- **Features** – Organized by editor sections (PropertyEditor, SceneExplorer, ContentBrowser, etc.)
- **Messages** – MVVM messaging contracts for inter-component communication
- **Routing** – Route definitions for navigation across editor views
- **Workspace** – Session and layout persistence
- **Utilities** – Converters, validators, and helper classes

## Key Features

- **Asset Management** – Browse, organize, and manage game assets with multiple layout views
- **Scene Editing** – Hierarchical scene tree with intuitive object manipulation
- **Property Inspection** – Component and property editing with multi-selection support
- **Real-time Preview** – Immediate visual feedback of changes
- **Document Management** – Multi-tab editing interface with organized workspaces
- **Flexible Layout** – Dockable windows and customizable panel arrangements
- **Project Integration** – Seamless integration with Oxygen Editor project system
- **Undo/Redo** – Full undo/redo support via TimeMachine integration
- **Logging & Diagnostics** – Integrated output console with application logging

## Development Workflow

### Build Commands

For targeted, efficient development builds:

```powershell
# Build just the WorldEditor project
dotnet build projects/Oxygen.Editor.World/src/Oxygen.Editor.World.csproj

# Build the entire solution
cd projects
.\open.cmd
```

### Testing

Tests follow MSTest conventions. Project tests should follow naming: `MethodName_Scenario_ExpectedBehavior`.

```powershell
# Run tests for the WorldEditor
dotnet test projects/Oxygen.Editor.World/tests/Oxygen.Editor.World.Tests/Oxygen.Editor.World.Tests.csproj

# Run tests with coverage
dotnet test /p:CollectCoverage=true /p:CoverletOutputFormat=lcov
```

### Repository-Wide Commands

```powershell
# Generate and open the full project solution
cd projects
.\open.cmd

# Clean all build artifacts
.\clean.ps1
```

## Coding Standards

### C# Style Guide

This project follows strict C# coding standards defined in `.github/instructions/csharp_coding_style.instructions.md`:

- **Language:** C# 13 preview features enabled
- **Nullability:** `nullable enable` – strict null checking enforced
- **Usings:** Implicit usings enabled
- **Access Modifiers:** Always explicit (`public`, `private`, `internal`, etc.)
- **Instance Members:** Use `this.` prefix for clarity
- **Code Analysis:** Strict analyzer rules enabled (StyleCop, Roslynator, Meziantou.Analyzer)

### MVVM Conventions

- **Views** – WinUI user controls, minimal code-behind, partial classes
- **ViewModels** – Inherit from `ObservableObject` (CommunityToolkit.Mvvm)
- **Auto-wiring** – Use `[ViewModel(typeof(TViewModel))]` attribute on Views; source generators handle wiring
- **Composition** – Favor composition over inheritance; keep ViewModels lean
- **DI** – Register services through the Hosting infrastructure, not manually

### Code Organization

- Avoid broad public APIs; prefer composition and focused interfaces
- Keep classes small and single-responsibility
- Use explicit access modifiers throughout
- Organize code logically; related functionality in same namespace

## Testing

This project uses **MSTest** for all unit tests:

- **Test Projects** – End with `.Tests` suffix (e.g., `Oxygen.Editor.World.Tests`)
- **UI Tests** – End with `.UI.Tests` suffix if requiring visual interface
- **Test Pattern** – Arrange-Act-Assert (AAA) style
- **Naming Convention** – `MethodName_Scenario_ExpectedBehavior`
- **Frameworks** – MSTest 4.0, AwesomeAssertions, Moq, Testably.Abstractions
- **Shared Utilities** – Reuse `projects/TestHelpers/` for common test infrastructure

Example test structure:

```csharp
[TestClass]
public class ExampleTests
{
    [TestMethod]
    public void Method_Scenario_ExpectedBehavior()
    {
        // Arrange
        var subject = new ExampleClass();

        // Act
        var result = subject.DoSomething();

        // Assert
        result.Should().Be(expectedValue);
    }
}
```

## Contributing

When contributing to the Oxygen World Editor, follow these guidelines:

1. **Read Project Documentation** – Review `design/` and `plan/` directories in the repository for architectural decisions
2. **Follow Code Style** – Adhere to the C# coding standards (see above)
3. **Use MVVM Patterns** – Follow existing View/ViewModel patterns and conventions
4. **Add Tests** – Include MSTest unit tests for new functionality
5. **Leverage DI** – Use dependency injection for services; register in the Hosting infrastructure
6. **Document Changes** – Update relevant documentation and code comments
7. **Small PRs** – Keep changes focused and maintainable
8. **Cross-Module Awareness** – Check if new public APIs exist in base modules (e.g., `Oxygen.Base`) before creating duplicates

For architectural questions or significant changes, open an issue referencing the relevant design documentation.

## License

This project is part of the DroidNet repository. See the [LICENSE](../../LICENSE) file for license information.

## References

- [DryIoc Documentation](https://github.com/dadhi/DryIoc)
- [CommunityToolkit.Mvvm](https://learn.microsoft.com/windows/communitytoolkit/mvvm/mvvm_introduction)
- [WinUI 3 Documentation](https://learn.microsoft.com/windows/apps/winui/)
- [Hosting Framework Documentation](../Hosting/README.md)
- [Routing Framework Documentation](../Routing/README.md)
- [Docking Framework Documentation](../Docking/README.md)
