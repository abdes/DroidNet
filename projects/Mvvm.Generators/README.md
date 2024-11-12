# C# Source generator for View to ViewModel wiring

**DroidNet.Mvvm.Generators** is a source generator package designed to simplify View-ViewModel wiring in MVVM-based applications using C# and Microsoft's [Roslyn](https://github.com/dotnet/roslyn) source generators. It generates the necessary boilerplate code for implementing `IViewFor<T>` interface on your views, reducing repetitive tasks and improving maintainability.

## Features

- Generates ViewModel-related code (e.g., dependency properties, events, and view model property) based on the `[ViewModel]` attribute used in your View classes.
- Supports incremental generation to improve build performance by only regenerating affected code when necessary.
- Provides diagnostic messages for invalid attribute annotations or missing templates.

## Usage

1. **Reference the generator and the attributes projects:**To use the `[ViewModel]` attribute in your project, you need to reference the attribute assembly (Mvvm.Generators.Attributes) properly. Add the following `ProjectReference` elements to your `.csproj` file:

```xml
<ItemGroup>
  <!-- Reference the generator and attribute projects as analyzers -->
  <ProjectReference Include="$(ProjectsRoot)\Mvvm.Generators\src\Mvvm.Generators.csproj" OutputItemType="Analyzer" ReferenceOutputAssembly="false" />
  <ProjectReference Include="$(ProjectsRoot)\Mvvm.Generators.Attributes\src\Mvvm.Generators.Attributes.csproj" OutputItemType="Analyzer" ReferenceOutputAssembly="true" />
</ItemGroup>
```

2. **Apply the ViewModel attribute:** Annotate your View classes with the `[ViewModel]` attribute, specifying the corresponding ViewModel type:

```csharp
[ViewModel(typeof(MyViewModel))]
public partial class MyView
{
    // ...
}
```

3. **Build your project:** The source generator will automatically generate the necessary boilerplate code for implementing `IViewFor<T>` interface in your View classes.
