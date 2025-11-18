# DroidNet MVVM Extensions

Provides MVVM (Model-View-ViewModel) helper components for .NET WinUI apps.

This library makes it straightforward to wire Views to ViewModels using a lightweight interface-based approach, a default view locator for automatic view resolution, and a XAML-friendly converter to transform a ViewModel into its View at runtime.

---

## Table of Contents

1. [Project Description](#1-project-description)
2. [Technology Stack](#2-technology-stack)
3. [Project Architecture](#3-project-architecture)
4. [Getting Started](#4-getting-started)
5. [Project Structure](#5-project-structure)
6. [Key Features](#6-key-features)
7. [Development Workflow and Conventions](#7-development-workflow-and-conventions)
8. [Coding Standards](#8-coding-standards)
9. [Testing](#9-testing)
10. [Contributing](#10-contributing)
11. [License](#11-license)

---

## 1. Project Description

DroidNet.Mvvm extends MVVM support for WinUI/.NET applications by:

- Defining a simple `IViewFor<T>` contract for Views that contain a typed ViewModel
- Providing `IViewLocator` (DefaultViewLocator) heuristics to locate Views for ViewModels using DI
- Offering a `ViewModelToView` converter for XAML-friendly runtime view resolution

Use this package to implement view-first or view-model-first patterns while keeping Views and ViewModels loosely coupled and testable.

## 2. Technology Stack

- C# / .NET 9 (target: `net9.0-windows10.0.26100.0`)
- WinUI 3 (Microsoft.WindowsAppSDK)
- Dependency injection: DryIoc
- Logging: Microsoft.Extensions.Logging
- Unit testing: MSTest, Moq, AwesomeAssertions

Package references are declared in the project file (`src/Mvvm.csproj`).

## 3. Project Architecture

- Interface-driven: `IViewFor`, `IViewFor<T>`, `IViewLocator`
- Default view resolution strategy: `DefaultViewLocator` resolves views using a series of heuristics (see code comments):
   1. Look up `IViewFor<T>` for the runtime ViewModel type or explicit ViewModel type
   2. Convert ViewModel name to a candidate View type name (default: `ViewModel` → `View`) and look up the view type
   3. Toggle between interface/class names (e.g., `IMyView` ↔ `MyView`) where appropriate
- Integration with DI: views should be registered with the DI container (DryIoc) as either `IViewFor<T>` or concrete view types
- XAML support: `ViewModelToView` converter uses `IViewLocator` to resolve a view and sets its `ViewModel` property before returning it for presentation

Example of `DefaultViewLocator` heuristics and behavior is implemented in `src/DefaultViewLocator.cs`.

## 4. Getting Started

Prerequisites:

- .NET 9 SDK
- WinUI 3 (Microsoft.WindowsAppSDK)

Quick start (register services and converter)

1. Register services in your host bootstrap or app startup code:

   ```cs
   services.AddSingleton<IViewLocator, DefaultViewLocator>();
   services.AddTransient<IViewFor<MyViewModel>, MyView>();
   ```

2. Add the `ViewModelToView` converter to your application resources so it is available in XAML:

   ```cs
   public partial class App
   {
         private const string VmToViewConverterResourceKey = "VmToViewConverter";

         public App([FromKeyedServices("Default")] IValueConverter vmToViewConverter)
         {
               this.InitializeComponent();
               Current.Resources[VmToViewConverterResourceKey] = vmToViewConverter;
         }
   }
   ```

3. Use in XAML (view-model-first pattern):

   ```xaml
   <ContentPresenter Content="{x:Bind ViewModel.Workspace, Converter={StaticResource VmToViewConverter}}" />
   ```

## 5. Project Structure

- `src/`
  - `IViewFor.cs` — defines `IViewFor` and `IViewFor<T>`
  - `IViewLocator.cs` — defines interface for resolving views
  - `DefaultViewLocator.cs` — default heuristics-based implementation
  - `Converters/ViewModelToView.cs` — XAML converter to resolve a View via `IViewLocator`
  - `Mvvm.csproj` — project file
- `tests/` — contains unit tests and UI-friendly tests
  - `DefaultViewLocatorTests.cs` — demonstrates the view resolution behavior
  - `Converters/ViewModelToViewTests.cs` — tests for the converter

Refer to the `tests/` folder for example usages and registration patterns.

## 6. Key Features

- `IViewFor<T>` and `IViewFor`: simple View/view-model contract with a typed ViewModel property and `ViewModelChanged`
- DefaultViewLocator: robust, heuristic-based view resolution that tries:
  - direct `IViewFor<T>` resolution by runtime or explicit VM type
  - convert `*ViewModel` name to `*View` and lookup the view type
  - toggle between interface/class forms of names (e.g., `IBaseView` ↔ `BaseView`)
- `ViewModelToView` converter: binds a ViewModel to an appropriate View in XAML and ensures the ViewModel property of the view is set
- DI integration with DryIoc; easily register views and view-locators as typical DI services
- Logging helpers, leveraging `ILoggerFactory` for diagnostics and resolution logs

## 7. Development Workflow and Conventions

- Build locally:

```pwsh
pwsh ./projects/Mvvm/scripts/build.ps1 # if present, otherwise
dotnet build projects/Mvvm/src/Mvvm.csproj
```

- Run tests:

```pwsh
dotnet test --project projects/Mvvm/tests/Mvvm.UI.Tests.csproj
```

- Follow MVVM patterns: prefer `IViewFor<T>` on Views, keep ViewModel logic free of UI concerns, use converters and view locators for glue code.

## 8. Coding Standards

- Follow repository-wide C# style guide (see `.github/instructions/csharp_coding_style.instructions.md`). Highlights:
  - Use explicit access modifiers
  - Use `this.` for instance members
  - Keep `nullable` enabled where required
  - Aim for small, testable classes and composition over inheritance

## 9. Testing

- Testing frameworks: MSTest with `MSTest` attribute usage
- Common libs used: `Moq` for mocks, `AwesomeAssertions` for fluent asserts
- Test naming: `MethodName_Scenario_ExpectedBehavior` (Arrange–Act–Assert)
- To run tests:

```pwsh
dotnet test --project projects/Mvvm/tests/Mvvm.UI.Tests.csproj
```

See `tests/DefaultViewLocatorTests.cs` for many real-world examples of view resolution and DI service mocking.

## 10. Contributing

Contributions are welcome (PRs, issues, or feedback). Guidelines:

- Fork the repo and create a feature branch
- Add tests whenever fixing bugs or adding features
- Follow the coding standards and commit message conventions used by this repo
- Add or update README and `design` docs for any architectural changes

If you're adding a public API or a breaking change, open an issue first and link to a design doc and rationale.

## 11. License

This project is distributed under the MIT License. See the repository `LICENSE` file for the full text.
