# Mvvm.Generators.Attributes

Annotation attributes for the [DroidNet.Mvvm.Generators](../Mvvm.Generators/) source generator. Provides the `[ViewModel]` attribute used to declaratively wire Views to ViewModels at compile time.

**Part of the [DroidNet](../../) mono-repository** — a modular, WinUI 3 + .NET 10 platform for building sophisticated desktop applications with source-generated dependency injection, incremental UI routing, and flexible docking layouts.

## Technology Stack

- **Language:** C#
- **Framework:** .NET Standard 2.0 (attribute library for use in source generator packages)
- **Build Output:** Centralized NuGet versioning via `Directory.Packages.props`
- **Distribution:** NuGet package (`DroidNet.Mvvm.Generators.Attributes`)

## Project Architecture

### Overview

This project provides a minimal attribute library consumed by the `Mvvm.Generators` source generator. The `ViewModelAttribute` is used by application developers to annotate View classes, signaling to the source generator that boilerplate MVVM infrastructure should be generated.

### Key Components

- **`ViewModelAttribute`:** Sealed attribute class (non-inheritable) that accepts a ViewModel type parameter and can only be applied to classes. Serves as a compile-time marker for the generator's syntax tree scanning phase.

### Design Rationale

- **Separate package:** By isolating the attribute definition in its own project, the attribute can be packaged independently and referenced without pulling in generator dependencies at runtime.
- **.NET Standard 2.0 target:** Ensures compatibility with a wide range of consuming projects (.NET Framework, .NET Core, .NET 5+).
- **Minimal surface area:** Contains only the attribute definition—no additional infrastructure or dependencies—to keep the package lightweight and focused.

## Getting Started

### Prerequisites

- .NET Standard 2.0+ supporting project or .NET 10+ SDK
- Visual Studio 2022 or VS Code with C# extension

### Installation

**Option 1: Via project reference** (in your application or library `.csproj`):

```xml
<ItemGroup>
  <ProjectReference Include="$(ProjectsRoot)\Mvvm.Generators.Attributes\src\Mvvm.Generators.Attributes.csproj"
                    OutputItemType="Analyzer"
                    ReferenceOutputAssembly="true" />
</ItemGroup>
```

**Option 2: Via NuGet package:**

```bash
dotnet add package DroidNet.Mvvm.Generators.Attributes
```

## Project Structure

```text
src/
  ├─ ViewModelAttribute.cs      # Attribute definition
  └─ Mvvm.Generators.Attributes.csproj
README.md                         # This file
```

## Key Features

- **ViewModelAttribute:** Declarative marker attribute for View-ViewModel associations
  - Accepts a `Type` parameter specifying the target ViewModel class
  - Applies only to class declarations
  - Works seamlessly with the `Mvvm.Generators` source generator
- **No Runtime Dependencies:** Attribute library contains zero external dependencies
- **Strong Type Safety:** Uses `typeof(T)` expressions for compile-time type checking

## Usage Example

To wire a View to a ViewModel, decorate your View class with the `[ViewModel]` attribute:

```csharp
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

[ViewModel(typeof(MyViewModel))]
public partial class MyView : UserControl
{
    // The Mvvm.Generators source generator will augment this class with:
    // - IViewFor<MyViewModel> interface implementation
    // - ViewModel dependency property
    // - ViewModelChanged event
}
```

The corresponding `Mvvm.Generators` source generator automatically generates the necessary boilerplate code, implementing `IViewFor<T>` and adding a strongly-typed `ViewModel` property with change notification support.

## Coding Standards

This project follows the DroidNet [C# coding standards](../../.github/instructions/csharp_coding_style.instructions.md):

- **C# 13 preview features** enabled (nullable annotations, implicit usings)
- **Explicit access modifiers** required (`public`, `private`, etc.)
- **`this.` prefix** for all instance member references
- **StyleCop.Analyzers** and **Roslynator** for static analysis

## Contributing

When modifying this project:

1. Keep the attribute lightweight and focused on its single responsibility
2. Ensure any changes maintain .NET Standard 2.0 compatibility
3. Follow the repository's [C# coding style guide](../../.github/instructions/csharp_coding_style.instructions.md)
4. Verify compatibility with the `Mvvm.Generators` source generator after making changes
5. Update version information in `Directory.Packages.props` if needed (do not specify versions in `.csproj` files)

For broader guidance, see the [DroidNet contributor instructions](../../.github/copilot-instructions.md).

## Related Projects

- **[Mvvm.Generators](../Mvvm.Generators/)** — The source generator that consumes this attribute library
- **[Mvvm](../Mvvm/)** — MVVM infrastructure and base classes
- **[Hosting](../Hosting/)** — .NET Generic Host integration for WinUI applications

## License

Distributed under the MIT License. See accompanying file LICENSE or copy at [https://opensource.org/licenses/MIT](https://opensource.org/licenses/MIT).
