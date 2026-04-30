# Oxygen Editor WorldEditor

A comprehensive game engine world editor built with **WinUI 3** and **.NET 9.0** that enables developers to design and manage virtual environments within the Oxygen game engine. This editor provides professional-grade tools for creating immersive worlds through terrain sculpting, object placement, lighting configuration, and asset management.

## Technology Stack

### Core Technologies

- **Platform**: Windows 11 (Windows App SDK 1.8.251106002)
- **Framework**: .NET 9.0 (Target Framework: `net9.0-windows10.0.26100.0`)
- **UI Framework**: WinUI 3 with XAML
- **Language**: C# 13 (preview features enabled)

### Key Dependencies

- **MVVM**: CommunityToolkit.Mvvm 8.4.0
- **DI Container**: DryIoc 6.0.0-preview-07
- **Routing**: Custom Angular-inspired navigation system with child routers
- **Logging**: Microsoft.Extensions.Logging with Serilog integration
- **Testing**: MSTest 4.0.2 with AwesomeAssertions

### UI Components

- **Controls**: CommunityToolkit.WinUI.Collections, Controls.Segmented, Controls.Triggers
- **Docking**: Flexible docking framework with tree-based layout management
- **Dynamic Tree**: Advanced tree view controls for hierarchical data display

## Project Architecture

### MVVM Architecture

The WorldEditor follows the **Model-View-ViewModel** pattern with the following structure:

- **Views**: WinUI 3 XAML pages with minimal code-behind
- **ViewModels**: CommunityToolkit.Mvvm-based classes with source-generated commands
- **Models**: Core game engine objects (SceneNode, Transform, etc.)

### Modular Design

The editor is built with a **modular architecture** featuring:

- **Child Containers**: Each workspace area uses isolated IoC containers for dependency resolution
- **Child Routers**: Local routing systems for modular navigation within each module
- **Messenger Pattern**: Inter-module communication using messenger services
- **Service Integration**: Comprehensive service integration for project management

### Core Components

#### 1. Workspace Management

- **Primary Coordinator**: `WorkspaceViewModel` manages the main docking workspace layout
- **Default Layout**: Organizes UI into multiple panels (renderer, scene explorer, properties, content browser, output)

#### 2. Content Browser

- **Dual Pane Interface**: Project tree navigation + asset display
- **Navigation Features**: Breadcrumb navigation, history management, hierarchical folder traversal
- **Asset Management**: Automatic indexing, scene creation, type categorization

#### 3. Scene Explorer

- **Hierarchical Tree View**: Dynamic tree control for scene object hierarchy
- **Scene Management**: Add/remove entities, undo/redo support, selection management
- **Integration**: Real-time selection broadcasting to properties editor

#### 4. Properties Editor

- **Dynamic Property Editing**: Shows properties for selected scene objects
- **Multi-Selection Support**: Handles editing multiple objects simultaneously
- **Component-Based**: Support for Transform and other entity components

#### 5. Document Host

- **Tabbed Interface**: Manages multiple documents with tabbed UI
- **Window Management**: Support for detached documents in separate windows

## Getting Started

### Prerequisites

- **Windows 11** (required for WinUI 3)
- **.NET 9.0 SDK** or later
- **Visual Studio 2022** or **VS Code** with C# extension

### Building the Project

1. **Clone the repository**:

   ```powershell
   git clone <repository-url>
   cd DroidNet
   ```

2. **Initialize the environment** (run once):

   ```powershell
   pwsh ./init.ps1
   ```

3. **Build the WorldEditor project**:

   ```powershell
   dotnet build projects/Oxygen.Editor.World/src/Oxygen.Editor.World.csproj
   ```

4. **Run the editor**:

   ```powershell
   dotnet run --project projects/Oxygen.Editor.World/src/Oxygen.Editor.World.csproj
   ```

### Development Commands

- **Clean build artifacts**:

  ```powershell
  .\clean.ps1
  ```

- **Run tests**:

  ```powershell
  dotnet test projects/Oxygen.Editor.World/tests/
  ```

- **Generate solution file**:

  ```powershell
  cd projects
  .\open.cmd
  ```

## Project Structure

```text
projects/Oxygen.Editor.World/src/
├── Constants.cs                           # Application constants
├── Oxygen.Editor.World.csproj       # Project file
├── README.md                              # This file
│
├── ContentBrowser/                        # Asset management and navigation
│   ├── AssetsViewModel.cs                 # Main assets view model
│   ├── ContentBrowserViewModel.cs         # Dual-pane browser coordinator
│   ├── AssetsIndexingService.cs           # Asset indexing and management
│   ├── ProjectLayoutViewModel.cs          # Project tree navigation
│   └── [Layout Views]                     # Tile/list layout implementations
│
├── Editors/                               # Document and tab management
│   ├── DocumentHostViewModel.cs           # Tabbed document manager
│   └── TabbedDocumentItem.cs              # Document item model
│
├── Messages/                              # Inter-module communication
│   ├── SceneNodeSelectionChangedMessage.cs
│   └── SceneNodeSelectionRequestMessage.cs
│
├── Output/                                # Console and logging output
│   └── OutputViewModel.cs                 # Output console manager
│
├── ProjectExplorer/                       # Project navigation
│   └── ProjectLayoutViewModel.cs          # Project tree implementation
│
├── PropertiesEditor/                      # Property editing system
│   ├── SceneNodeEditorViewModel.cs        # Main properties editor
│   ├── TransformViewModel.cs              # Transform component editor
│   └── PropertiesExpander.cs              # Property display control
│
├── SceneExplorer/                         # Scene hierarchy management
│   ├── SceneExplorerViewModel.cs          # Scene tree view model
│   ├── SceneAdapter.cs                    # Scene tree adapter
│   └── SceneNodeAdapter.cs                # Scene node adapter
│
├── Routing/                               # Navigation system
│   ├── LocalRouterContext.cs              # Local routing context
│   └── LocalRoutingExtensions.cs          # Routing extension methods
│
├── Styles/                                # UI styling
│   └── CommandBarStyles.xaml              # Command bar styling
│
├── Themes/                                # Theme resources
│   └── Generic.xaml                       # Generic theme definitions
│
├── Utils/                                 # Utility services
│   └── ThumbnailGenerator.cs              # Asset thumbnail generation
│
└── Workspace/                             # Docking workspace
    ├── WorkspaceViewModel.cs              # Main workspace coordinator
    ├── DockingWorkspaceViewModel.cs       # Docking layout manager
    └── DockViewFactory.cs                 # Dock view factory
```

## Key Features

### 🎮 Game Development Tools

- **Scene Management**: Create, load, and manage multiple game scenes
- **Entity Hierarchy**: Tree-based scene object organization and manipulation
- **Asset Management**: Comprehensive asset import, organization, and indexing
- **Property Editing**: Real-time property editing with multi-selection support

### 🖥️ Professional Interface

- **Docking Layout**: Flexible, customizable workspace layout
- **Multi-Document Support**: Tabbed interface with window detachment capabilities
- **Navigation System**: Breadcrumb navigation, history management, hierarchical exploration
- **Real-Time Updates**: Live updates across all components via messenger system

### 🏗️ Advanced Architecture

- **Modular Design**: Isolated containers and routers for each workspace area
- **Service-Oriented**: Comprehensive service integration for project management
- **Undo/Redo Support**: Full history tracking for all scene modifications
- **Component System**: Support for entity components (Transform, etc.)

### 🎨 User Experience

- **Intuitive Layout**: Familiar IDE-like interface with docking panels
- **Efficient Navigation**: Multiple navigation methods (breadcrumbs, tree, history)
- **Responsive UI**: Real-time updates and smooth interactions
- **Theme Support**: Full light/dark theme integration

## Development Workflow

### Code Organization

- **MVVM Pattern**: Follow strict Model-View-ViewModel separation
- **Source Generation**: Use CommunityToolkit.Mvvm source generators for commands
- **Dependency Injection**: Register services using DryIoc via the Hosting layer
- **Modular Development**: Each module has isolated dependencies and routing

### Coding Standards

- **C# 13**: Use preview features with strict analysis enabled
- **Explicit Access Modifiers**: Always specify `public`, `private`, `protected`, etc.
- **Nullable Reference Types**: Enable nullable reference types
- **Style Guidelines**: Follow repository-wide coding standards in `.github/instructions/csharp_coding_style.instructions.md`

### Testing Approach

- **MSTest Framework**: Use MSTest for all unit tests
- **AAA Pattern**: Follow Arrange-Act-Assert testing pattern
- **Test Naming**: Use descriptive test names: `MethodName_Scenario_ExpectedBehavior`
- **Coverage**: Maintain high test coverage for critical functionality

## Contributing

### Development Guidelines

1. **Follow Existing Patterns**: Study existing ViewModels and services before implementing new features
2. **Use Source Generators**: Leverage CommunityToolkit.Mvvm source generators for command generation
3. **Maintain Modular Design**: Keep modules isolated with their own IoC containers when appropriate
4. **Document Decisions**: Add design documentation for non-obvious architectural decisions

### Code Examples

- **ViewModel Creation**: Inherit from `ObservableObject` and use `[RelayCommand]` attributes
- **Service Registration**: Register services in child containers using Microsoft.Extensions patterns
- **Routing**: Use the local routing system with outlets and matrix parameters
- **Messenger Communication**: Use `IMessenger` for inter-module communication

### Before Submitting

- Ensure all tests pass: `dotnet test`
- Follow coding standards: Run StyleCop analyzers
- Update documentation: Add XML documentation for public APIs
- Test user scenarios: Verify core workflows work as expected

## License

This project is licensed under the **MIT License** - see the [LICENSE](../../LICENSE) file for details.

Copyright © 2024 Abdessattar Sassi. This product is licensed under the MIT License.

---

## Related Documentation

- [Oxygen Editor Core](../Oxygen.Managed.Core/README.md)
- [DroidNet Repository Documentation](../../.github/copilot-instructions.md)
- [C# Coding Standards](../../.github/instructions/csharp_coding_style.instructions.md)
- [WinUI 3 Guidelines](../../.github/instructions/csharp_coding_style.instructions.md#winui-3-specific-guidelines)
