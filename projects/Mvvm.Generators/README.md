# Mvvm.Generators — C# Source Generator for View-ViewModel Wiring

**DroidNet.Mvvm.Generators** is a C# source generator package that automates View-ViewModel wiring in MVVM-based WinUI applications. Using the `[ViewModel]` attribute and Microsoft's [Roslyn](https://github.com/dotnet/roslyn) compiler platform, it generates the necessary boilerplate code for implementing `IViewFor<T>` interface on your views, eliminating repetitive tasks and improving code maintainability.

Part of the **DroidNet** mono-repository — a modular, WinUI 3 + .NET 10 platform for building sophisticated desktop applications with source-generated dependency injection, incremental UI routing, and flexible docking layouts.

## Project Name and Description

**Mvvm.Generators** provides compile-time code generation for declarative View-ViewModel binding in WinUI applications. It transforms minimal attribute annotations into production-ready MVVM infrastructure, reducing boilerplate while maintaining strong type safety and IDE support.

## Technology Stack

- **Language:** C# 13 (preview features enabled)
- **Framework:** .NET Standard 2.0 (as a source generator package)
- **Compiler Platform:** Microsoft.CodeAnalysis (Roslyn) for syntax analysis and code generation
- **Template Engine:** HandlebarsDotNet for template rendering
- **Testing:** MSTest 4.0 with snapshot testing (Verify library)
- **Target Platforms:** WinUI 3 applications (.NET 10)
- **Build Output:** Artifacts layout (UseArtifactsOutput=true) with centralized NuGet versioning

## Project Architecture

### Overview

The generator operates as an **incremental source generator** within the Roslyn compiler pipeline:

1. **Syntax scanning phase:** Detects classes decorated with `[ViewModel(typeof(T))]` attributes
2. **Validation phase:** Verifies attribute usage and resolves type symbols
3. **Code generation phase:** Applies a Handlebars template with type parameters to produce generated code
4. **Diagnostic reporting:** Issues compilation errors for malformed attributes

### Key Components

- **`ViewModelWiringGenerator.cs`:** Main incremental generator implementing `IIncrementalGenerator`. Scans syntax trees for `[ViewModel]` attributes and triggers code generation via templates.
- **`ViewForTemplate.txt`:** Embedded Handlebars template producing the generated View class implementation with:
  - `IViewFor<T>` interface implementation
  - `ViewModelProperty` dependency property
  - `ViewModel` property getter/setter
  - `ViewModelChanged` event for property change notifications
- **`ViewForTemplateParameters.cs`:** Data transfer object containing view/viewmodel class names and namespaces for template substitution.
- **Mvvm.Generators.Attributes:** Companion package containing the `[ViewModel]` attribute definition (used by consuming projects).

### Design Rationale

- **Embedded templates:** Templates are embedded as resources (not external dependencies) to avoid tooling complications in source generator projects.
- **Manual template substitution:** Uses Handlebars instead of external templating libraries to minimize package dependencies for a compiler-pipeline component.
- **Incremental generation:** Leverages Roslyn's incremental API to regenerate only affected classes when source changes, improving build performance.

## Getting Started

### Prerequisites

- .NET 10+ SDK (or compatible .NET Standard 2.0 consuming project)
- Visual Studio 2022 or VS Code with C# extension
- Basic understanding of MVVM patterns and WinUI

### Installation

**Step 1:** Add project references to your View/ViewModel project's `.csproj` file:

```xml
<ItemGroup>
  <!-- Reference the generator and attributes as analyzers -->
  <ProjectReference Include="$(ProjectsRoot)\Mvvm.Generators\src\Mvvm.Generators.csproj"
                    OutputItemType="Analyzer"
                    ReferenceOutputAssembly="false" />
  <ProjectReference Include="$(ProjectsRoot)\Mvvm.Generators.Attributes\src\Mvvm.Generators.Attributes.csproj"
                    OutputItemType="Analyzer"
                    ReferenceOutputAssembly="true" />
</ItemGroup>
```

Alternatively, use the NuGet packages (when published):

```bash
dotnet add package DroidNet.Mvvm.Generators
dotnet add package DroidNet.Mvvm.Generators.Attributes
```

**Step 2:** Create your ViewModel following community conventions (inherit from `ObservableObject` or equivalent):

```csharp
namespace MyApp.ViewModels;

using CommunityToolkit.Mvvm.ComponentModel;

public partial class HomeViewModel : ObservableObject
{
    [ObservableProperty]
    private string title = "Welcome";

    public void LoadData() => Title = "Data Loaded";
}
```

**Step 3:** Annotate your View class with `[ViewModel]`:

```csharp
namespace MyApp.Views;

using DroidNet.Mvvm.Generators;
using MyApp.ViewModels;
using Microsoft.UI.Xaml.Controls;

[ViewModel(typeof(HomeViewModel))]
public partial class HomePage : Page
{
    public HomePage()
    {
        this.InitializeComponent();
    }
}
```

**Step 4:** Build your project. The source generator automatically:

- Generates `HomePage.g.cs` with `IViewFor<HomeViewModel>` implementation
- Creates a `ViewModelProperty` dependency property
- Wires up property change notifications

**Step 5:** Access the ViewModel in your code-behind or XAML bindings:

```csharp
// In code-behind
var view = new HomePage();
view.ViewModel = new HomeViewModel();
view.ViewModel.LoadData();
```

Or in XAML with binding:

```xaml
<Page x:Name="root">
    <TextBlock Text="{Binding Title, ElementName=root, Mode=OneWay}" />
</Page>
```

## Project Structure

```text
projects/Mvvm.Generators/
├── README.md                          # This file
├── open.cmd                           # Solution generator helper
│
├── src/
│   ├── Mvvm.Generators.csproj        # Main generator project
│   ├── ViewModelWiringGenerator.cs    # IIncrementalGenerator implementation
│   ├── Properties/
│   │   └── launchSettings.json        # Debug configuration
│   └── Templates/
│       ├── ViewForTemplate.txt        # Handlebars template for generated View class
│       └── ViewForTemplateParameters.cs  # Template data model
│
├── tests/
│   ├── Unit/
│   │   ├── Mvvm.Generators.Tests.csproj
│   │   ├── ViewModelWiringGeneratorTests.cs     # Snapshot-based unit tests
│   │   ├── TestHelper.cs              # Roslyn compilation utilities
│   │   └── Snapshots/                 # Verified snapshot outputs
│   │       ├── ViewModelWiringGeneratorTests.GenerateViewExtensionsCorrectly.verified.txt
│   │       └── ViewModelWiringGeneratorTests.IssueDiagnosticWhenMalformedAttribute.verified.txt
│   │
│   └── Integration/
│       ├── Mvvm.Generators.Integration.UI.Tests.csproj  # WinUI app-level tests
│       ├── App.xaml / App.xaml.cs
│       ├── Demo/
│       │   ├── DemoView.cs            # Sample View with [ViewModel] attribute
│       │   └── DemoViewModel.cs       # Sample ViewModel
│       └── TestEnv.cs                 # Test environment setup
│
└── ../Mvvm.Generators.Attributes/     # Companion attributes package
    └── src/
        ├── ViewModelAttribute.cs      # [ViewModel] attribute definition
        └── Mvvm.Generators.Attributes.csproj
```

## Key Features

- **Declarative binding:** Use `[ViewModel(typeof(T))]` to declare View-ViewModel relationships at design time
- **Generated infrastructure:** Automatic `IViewFor<T>` implementation with dependency properties and event notifications
- **Type-safe:** Full compile-time checking of ViewModel types; errors reported as diagnostics
- **Incremental compilation:** Only regenerates code for modified views, speeding up build iteration
- **Diagnostic support:** Clear error messages for invalid attribute usage (e.g., non-existent types, malformed attributes)
- **Template-based:** Extensible via Handlebars templates; easy to customize generated code structure

## Development Workflow

### Building

Build the generator and tests locally:

```powershell
# Build generator and attributes
dotnet build projects/Mvvm.Generators/src/Mvvm.Generators.csproj
dotnet build projects/Mvvm.Generators.Attributes/src/Mvvm.Generators.Attributes.csproj

# Build unit tests
dotnet build projects/Mvvm.Generators/tests/Unit/Mvvm.Generators.Tests.csproj

# Build integration tests (WinUI app-level)
dotnet build projects/Mvvm.Generators/tests/Integration/Mvvm.Generators.Integration.UI.Tests.csproj
```

### Testing

The project uses **MSTest** with snapshot testing:

```powershell
# Run unit tests with snapshot verification
dotnet test projects/Mvvm.Generators/tests/Unit/Mvvm.Generators.Tests.csproj

# Run integration tests
dotnet test projects/Mvvm.Generators/tests/Integration/Mvvm.Generators.Integration.UI.Tests.csproj

# Run with coverage
dotnet test projects/Mvvm.Generators/tests/Unit/Mvvm.Generators.Tests.csproj /p:CollectCoverage=true /p:CoverletOutputFormat=lcov
```

### Debugging Generators

To debug the source generator during development:

1. Set breakpoints in `ViewModelWiringGenerator.cs`
2. Open the integration test project's `.csproj` and uncomment debug configuration (if present)
3. Build and the debugger will attach to the compiler process

**Resources:**

- [Debugging source generators (Roslyn SDK)](https://github.com/dotnet/roslyn-sdk/issues/850)
- [Syntax Visualizer (VS extension)](https://learn.microsoft.com/en-us/dotnet/csharp/roslyn-sdk/syntax-visualizer?tabs=csharp)

## Coding Standards

This project follows the **DroidNet C# coding conventions**:

- **Language features:** C# 13 preview features, `nullable` enabled, `ImplicitUsings` enabled
- **Access modifiers:** Always explicit (`public`, `private`, etc.)
- **Instance members:** Prefixed with `this.` for clarity
- **Naming conventions:** PascalCase for public members, camelCase for local variables/parameters
- **Code analysis:** StyleCop.Analyzers and Roslynator enforced; suppressions must be justified
- **Source generator specifics:**
  - Embedded resources for templates (no external file dependencies)
  - Comprehensive XML documentation on public APIs
  - Graceful error handling with diagnostic reporting (not exceptions to end users)

See `.github/instructions/csharp_coding_style.instructions.md` for full guidelines.

## Testing Approach

**Unit tests** use snapshot testing with the Verify library to ensure generated code remains correct across refactorings:

1. **Arrange:** Define sample View/ViewModel classes with various configurations
2. **Act:** Compile source code with the generator attached
3. **Assert:** Verify generated output matches saved snapshots

**Integration tests** provide end-to-end validation:

- Launch a test WinUI application with `[ViewModel]` decorated views
- Verify runtime behavior (property binding, event notifications)
- Test interaction with the DI container (Hosting layer integration)

Example test case:

```csharp
[TestMethod]
public Task GenerateViewExtensionsCorrectly()
{
    const string source = """
        namespace Testing;

        using DroidNet.Mvvm.Generators;

        [ViewModel(typeof(TestViewModel))]
        public partial class TestView { }

        public class TestViewModel;
        """;

    return Verify(GetGeneratedOutput(source));
}
```

## Contributing

Contributions are welcome! Please follow these guidelines:

1. **Read the architecture:** Review `ViewModelWiringGenerator.cs` and existing tests
2. **Follow code exemplars:** Check unit and integration tests for patterns
3. **Reference design docs:** See `plan/` for architectural decisions
4. **Write tests first:** Use snapshot testing for new generator features
5. **Update templates carefully:** Changes to `ViewForTemplate.txt` affect all generated View classes
6. **Test both paths:** Verify unit tests pass and integration tests compile successfully

## License

Distributed under the **MIT License**. See `LICENSE` file in the repository root.

## References

- [Roslyn Source Generators Documentation](https://github.com/dotnet/roslyn/blob/main/docs/features/source-generators/)
- [DroidNet Mono-Repository](../README.md)
- [MVVM Base Module](../Mvvm/)
- [Hosting Layer (DI Integration)](../Hosting/)
