# README

<!-- markdownlint-disable-next-line no-inline-html -->
<div align="center">

[![Windows][windows-badge]][WinUI]
[![pre-commit][pre-commit-badge]][pre-commit]

</div>

> A toolkit and example for mono-repo style WindowsAppSdk development with
> Visual Studio.

[windows-badge]: https://img.shields.io/badge/OS-windows-blue
[WinUI]: https://learn.microsoft.com/en-us/windows/apps/winui/
[pre-commit-badge]: https://img.shields.io/badge/pre--commit-enabled-brightgreen?logo=pre-commit
[pre-commit]: https://github.com/pre-commit/pre-commit

## Overview

“DroidNet” is envisioned as a comprehensive suite of sub-projects aimed at
automating and streamlining the development, testing, and continuous integration
of WinUI apps. “DroidNet” is designed to be a robust and comprehensive solution
for developing WinUI applications. Its focus on automation and quality
assurance, combined with its modular architecture, makes it a powerful tool for
any developer working with WinUI and WinAppSDK. Happy coding!

## Key Features

"DroidNet" has been designed with a focus on the following features:

- **Automation**: Automate repetitive tasks in your development process, such as
  building, testing, and deployment.

- **Modularity**: The project has been structured as a mono-repo, allowing for
  easy management and separation of concerns between different sub-projects.

- **Quality Assurance**: Includes built-in tools for code linting and
  formatting, as well as pre-commit hooks to ensure code quality before commits.

- **Integration with WinUI and WinAppSDK**: The project is designed to work
  seamlessly with WinUI and WinAppSDK, allowing developers to leverage the
  latest technologies in Windows app development.

### Docking framework for WinUI 3

The [Docking](projects/Docking/) project contains a flexible docking frameowrk
for WinUI. Dockable views can be embedded in Docks, which are managed in a tree
structure by a Docker. Combined with a pluggable layout engine, the docking tree
can be rendered into dock panels which can be attached to the workspace edges or
relative to other docks.

![Example docking workspace](media/routing-debugger.png "Docking Workspace")

### MVVM ViewModel first router

Similar to what Angular does in a web application, the [Router](projects/Routing/)
provides a routing frameowrk to navigate within the WinUI application using URIs.
With the provided source generators, it is easy to declare views, view models, and
wire them together. The routing configuration is completely declarative and follows
the same principles than Angular.

### Application host and Dependency Container

The [Hosting](projects/Hosting/) project offers an integration with .Net Host and
the DryIoc container. The source generators automate the injection of services and
view models and a view locator service makes it intuitive to locate a view for a
particular view model.

## Projects Overview

DroidNet is organized as a modular mono-repo with specialized projects for different aspects of WinUI application development:

### Core Infrastructure

- **[Hosting](projects/Hosting/)** - Integration of .NET Generic Host for WinUI applications, providing dependency injection, configuration, logging, and application lifecycle management with a `UserInterfaceHostedService` for managing UI as a background service.

- **[Mvvm](projects/Mvvm/)** - MVVM infrastructure with intelligent view resolution, strong typing via `IViewFor<T>` interface, and XAML-friendly converters for binding ViewModels to Views.

- **[Routing](projects/Routing/)** - Angular-inspired URI-based navigation framework with support for outlets, matrix parameters, nested routes, and declarative route configuration for WinUI applications.

- **[Bootstrap](projects/Bootstrap/)** - Reusable bootstrapping library providing a fluent configuration API for setting up dependency injection, logging, routing, MVVM, and WinUI integration in a single coherent startup flow.

### UI & Presentation Layer

- **[Docking](projects/Docking/)** - Flexible docking framework for managing dockable panels with tree-based layout management, support for multiple dock types (CenterDock, ToolDock), tab-based interfaces, and resizable panels.

- **[Controls](projects/Controls/)** - Custom WinUI 3 controls with best practices for implementation, including DynamicTreeItem and other reusable components. Demonstrates proper patterns for dependency properties, template parts, visual states, and styling.

- **[Aura](projects/Aura/)** - Comprehensive WinUI 3 shell framework providing window management, custom decorations, theming, OS notifications, and taskbar integration. Handles all UI infrastructure around core business logic.

- **[Converters](projects/Converters/)** - Collection of value converters for WinUI XAML binding, including `BoolToBrushConverter`, `NullToVisibilityConverter`, `ItemClickEventArgsToClickedItemConverter`, and `DictionaryValueConverter`.

### Data & Configuration

- **[Config](projects/Config/)** - Configuration management services including `PathFinder` for environment-aware path resolution and MVVM-friendly settings services with change tracking, validation, and durable persistence.

- **[Collections](projects/Collections/)** - Extension methods and custom collections for working with `ObservableCollection<T>`, including `DynamicObservableCollection` for transforming collections and sorted insertion helpers.

- **[Coordinates](projects/Coordinates/)** - Type-safe spatial coordinate transformations for WinUI with multi-monitor DPI awareness, supporting conversion between element, window, and screen coordinate spaces.

- **[Resources](projects/Resources/)** - Lightweight localization library with extension helpers for retrieving localized strings from resource maps with graceful fallback behavior.

### Code Generation & Tools

- **[Mvvm.Generators](projects/Mvvm.Generators/)** - C# source generator for automatic View-ViewModel wiring using `[ViewModel]` attribute, reducing boilerplate and improving maintainability.

- **[Mvvm.Generators.Attributes](projects/Mvvm.Generators.Attributes/)** - Attribute types for MVVM source generators, providing the `[ViewModel]` attribute for decorating View classes.

- **[Resources.Generator](projects/Resources.Generator/)** - Source generator that emits per-assembly localization helpers (`L()` and `R()` extension methods) for simplified resource access.

### Utilities & Testing

- **[TimeMachine](projects/TimeMachine/)** - Undo/Redo infrastructure for implementing undo/redo functionality in applications.

- **[TestHelpers](projects/TestHelpers/)** - Testing utilities providing logging configuration, dependency injection setup, assertion handling, and event handler testing helpers for unit tests.

- **[UITests.Shared](projects/UITests.Shared/)** - Shared utilities and helpers for UI testing across projects.

### Oxygen Game Engine and Editor

The Oxygen projects represent a comprehensive game editor and engine implementation built on top of DroidNet:

- **[Oxygen.Engine](projects/Oxygen.Engine/)** - C++ game engine with bindless rendering capabilities, providing the runtime for the Oxygen Editor.

- **[Oxygen.Editor](projects/Oxygen.Editor/)** - Main game editor application built with WinUI 3, integrating all DroidNet components for a professional IDE-like experience.

- **[Oxygen.Core](projects/Oxygen.Core/)** - Core libraries providing path finding, input validation, and essential services for the Oxygen Editor.

- **[Oxygen.Editor.Interop](projects/Oxygen.Editor.Interop/)** - Interoperability layer between managed code and the C++ engine.

- **[Oxygen.Editor.ProjectBrowser](projects/Oxygen.Editor.ProjectBrowser/)** - Project browsing and management UI component.

- **[Oxygen.Editor.Projects](projects/Oxygen.Editor.Projects/)** - Project file and structure management.

- **[Oxygen.Storage](projects/Oxygen.Storage/)** - Persistence and data storage services for projects and editor state.

- **[Oxygen.Editor.WorldEditor](projects/Oxygen.Editor.WorldEditor/)** - World editing and scene management UI.

- **[Oxygen.Editor.Data](projects/Oxygen.Editor.Data/)** - Data models and serialization for world and project data.

## Getting Started

To get started with "DroidNet", you'll need to have Visual Studio installed on
your machine. You can then clone the repository and open the solution in Visual
Studio to start developing. Simply use the `open.cmd` script in any of the folders
to generate the solution file and open it in Visual Studio.

## Contributions

Contributions to "DroidNet" are welcome! If you have a feature request, bug
report, or want to contribute to the code, please feel free to open an issue or
a pull request.

## License

"DroidNet" is licensed under the MIT License. See the LICENSE file for more
information.
