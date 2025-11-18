# DroidNet Converters

A comprehensive collection of `IValueConverter` implementations for WinUI 3 applications, providing essential data transformation utilities for XAML data binding scenarios.

## Project Description

The Converters module is a utility library within the DroidNet mono-repo that provides reusable value converters for WinUI 3 applications. These converters handle common data transformation patterns required in XAML bindings, enabling clean separation between data models and UI presentation logic.

## Technology Stack

- **Framework**: .NET 9.0 (net9.0-windows10.0.26100.0)
- **Platform**: Windows 10 (Build 26100.0)
- **UI Framework**: WinUI 3 / Microsoft.WindowsAppSDK
- **Testing**: MSTest with AwesomeAssertions
- **Language**: C# 13 (preview features enabled)

## Architecture

The project follows a modular architecture consistent with the DroidNet mono-repo:

```plaintext
Converters/
├── src/
│   ├── Converters.csproj          # Main library project
│   ├── BoolToBrushConverter.cs     # Bool → Brush conversion
│   ├── NullToVisibilityConverter.cs # Null → Visibility conversion
│   ├── DictionaryValueConverter`1.cs # Dictionary lookup converter
│   ├── IndexToNavigationItemConverter.cs # Index → NavigationView item
│   └── ItemClickEventArgsToClickedItemConverter.cs # Event args → Item
└── tests/
    ├── Converters.UI.Tests.csproj  # UI test project
    └── Tests/
        └── *Test.cs                # Unit tests for each converter
```

### Design Principles

- **Single Responsibility**: Each converter handles one specific transformation pattern
- **Stateless**: Converters are stateless objects suitable for XAML static resources
- **Composable**: Converters can be combined in binding chains
- **Nullable-Safe**: All converters respect nullable reference type contracts (C# nullable enabled)

## Getting Started

### Prerequisites

- [.NET 9.0 SDK](https://dotnet.microsoft.com/download/dotnet/9.0) or later
- Windows 10 (Build 26100.0) or later for runtime execution
- Visual Studio 2022 (recommended) or VS Code with C# DevKit

### Installation

#### Option 1: NuGet Package

```bash
dotnet add package DroidNet.Converters
```

#### Option 2: Project Reference

Add a project reference in your `.csproj` file:

```xml
<ItemGroup>
    <ProjectReference Include="..\path\to\Converters\src\Converters.csproj" />
</ItemGroup>
```

#### Option 3: Build from Source

```bash
# Clone the repository and navigate to the Converters project
cd projects/Converters

# Build the library
dotnet build src/Converters.csproj

# Run tests
dotnet test tests/Converters.UI.Tests.csproj
```

## Project Structure

### Source Layout (`src/`)

The library is organized around converter functionality:

- **Core Converters**: Implementation files for each converter type
- **Namespace**: All public types are in the `DroidNet.Converters` namespace
- **Accessibility**: All types are `public` and include comprehensive XML documentation

### Test Layout (`tests/`)

The test project contains:

- **UI Test Project**: `Converters.UI.Tests.csproj` targeting WinUI test runtime
- **Test Organization**: Tests organized by converter with AAA (Arrange-Act-Assert) pattern
- **Test Framework**: MSTest with AwesomeAssertions for fluent assertions

## Key Features

### 1. BoolToBrushConverter

Converts boolean values to Brush objects for UI styling.

**Use Case**: Toggle visual states (enabled/disabled, active/inactive) with brushes

**Properties**:

- `ActiveBrush`: Brush when value is `true` (default: Red)
- `InactiveBrush`: Brush when value is `false` (default: Transparent)

### 2. NullToVisibilityConverter

Converts null reference checks to visibility states for conditional UI display.

**Use Case**: Show/hide UI elements based on data presence

```xaml
<converters:NullToVisibilityConverter x:Key="NullToVis"/>

<TextBlock Text="No data" Visibility="{Binding Data,
    Converter={StaticResource NullToVis}, ConverterParameter=Visible}"/>
```

**Behavior**:

- Returns `Visibility.Visible` when value is not null
- Returns custom visibility (via parameter) or `Visibility.Collapsed` when value is null
- Parameter accepts "Visible", "Collapsed", or "Hidden"

### 3. ItemClickEventArgsToClickedItemConverter

Extracts the clicked item from `ItemClickEventArgs` event arguments.

**Use Case**: Binding ItemClick event to command with typed item

```xaml
<ListView ItemClick="ListView_ItemClick"
    ItemsSource="{Binding Items}"/>
```

**Behavior**: Extracts `ItemClickEventArgs.ClickedItem` and passes it to the command handler

### 4. IndexToNavigationItemConverter

Maps integer indices to navigation items in a NavigationView.

**Use Case**: Synchronizing selected index with navigation items

**Usage**: Requires constructor parameters:

- `NavigationView`: The containing NavigationView control
- `GetNavigationItems`: Delegate providing the current item list

**Behavior**:

- Returns the item at the specified index
- Special handling for `int.MaxValue` (returns SettingsItem)
- Returns null for out-of-range indices

### 5. DictionaryValueConverter (generic)

Generic converter for dictionary key-to-value lookups.

**Use Case**: Translating enum/string keys to display values

```csharp
// Define a derived converter for type safety
public class StringByKeyConverter : DictionaryValueConverter<string> { }
```

```xaml
<converters:StringByKeyConverter x:Key="StringLookup"
    Dictionary="{Binding LookupTable}"/>

<TextBlock Text="{Binding ItemKey, Converter={StaticResource StringLookup},
    ConverterParameter=ItemKey}"/>
```

**Generic Parameter `T`**: Type of values stored in the dictionary

**Behavior**:

- Looks up dictionary value by string key (provided as ConverterParameter)
- Returns null if key not found or dictionary is null

## Development Workflow

### Building

```bash
# Build the library only
dotnet build projects/Converters/src/Converters.csproj

# Build with tests
dotnet build projects/Converters/
```

### Testing

```bash
# Run unit tests
dotnet test projects/Converters/tests/Converters.UI.Tests.csproj

# Run with coverage
dotnet test projects/Converters/tests/Converters.UI.Tests.csproj /p:CollectCoverage=true
```

### Code Generation

The project uses source generators indirectly through:

- **XAML Resources**: Converters are typically registered as static XAML resources
- **No Manual Registration**: Converters are designed as simple instantiable classes

### Project Integration

From the mono-repo root:

```powershell
# Open the Converters solution
cd projects/Converters
.\open.cmd
```

## Coding Standards

The project adheres to DroidNet repository conventions:

### C# Style Guidelines

- **Language Version**: C# 13 with preview features enabled
- **Nullable References**: Enabled (`<Nullable>enable</Nullable>`)
- **Implicit Usings**: Enabled
- **Access Modifiers**: Always explicit (`public`, `private`, `internal`)
- **Instance Member Prefix**: Always use `this.` when referencing instance members
- **Braces**: Required for all control statements, even single-line
- **Code Analysis**: Full enforcement with `AnalysisMode=All`

### XAML/WinUI Conventions

- Follow MVVM patterns: converters are utility components supporting ViewModel bindings
- Minimize converter logic: complex transformations should occur in ViewModels
- Theme-Aware: Use theme-aware resources when defining default brushes
- Documentation: Include XML documentation on all public types and members

### Naming Conventions

- **Class Names**: `[TransformType]To[TargetType]Converter` (e.g., `BoolToBrushConverter`)
- **Generic Derivatives**: `[TargetType]ByKeyConverter` for generic converter specializations
- **Test Classes**: `[ConverterName]Test` (e.g., `NullToVisibilityConverterTest`)

## Testing

### Test Framework

- **Framework**: MSTest (`[TestClass]`, `[TestMethod]` attributes)
- **Assertions**: AwesomeAssertions for fluent assertion chains
- **Naming Pattern**: `MethodName_Scenario_ExpectedBehavior`
- **Structure**: AAA pattern (Arrange-Act-Assert)
- **Exclusions**: `[ExcludeFromCodeCoverage]` on test classes

### Test Categories

Tests are organized using `[TestCategory]`:

- Per-converter tests: `"[ConverterName]"`
- Test type: `"UITest"`

### Example Test

```csharp
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Null To Visibility")]
[TestCategory("UITest")]
public class NullToVisibilityConverterTest
{
    private readonly NullToVisibilityConverter converter = new();

    [TestMethod]
    public void NotNullValue_ProducesVisible()
    {
        // Arrange
        const string nonNullValue = "Hello";

        // Act
        var result = this.converter.Convert(nonNullValue, typeof(Visibility),
            parameter: null, "en-US");

        // Assert
        _ = result.Should().Be(Visibility.Visible);
    }
}
```

## Contributing

### Before You Start

1. Read the [DroidNet Copilot Instructions](./../.github/copilot-instructions.md)
2. Review the [C# Coding Style Guide](./../.github/instructions/csharp_coding_style.instructions.md)
3. Check existing converters for patterns and conventions

### Implementation Guidelines

When adding new converters:

1. **Follow Naming Convention**: `[Type1]To[Type2]Converter`
2. **Implement IValueConverter**: Provide both `Convert` and `ConvertBack` methods
3. **Add XML Documentation**: Document public members, parameters, return values, and exceptions
4. **Handle Edge Cases**: Validate input types and gracefully handle invalid conversions
5. **Create Tests**: Include unit tests following the AAA pattern
6. **Update Documentation**: Add converter description to this README

### Code Example: Creating a New Converter

```csharp
namespace DroidNet.Converters;

/// <summary>
/// Converts [description] to [result type].
/// </summary>
public partial class MyTypeToOtherTypeConverter : IValueConverter
{
    /// <summary>
    /// Converts [description].
    /// </summary>
    public object? Convert(object? value, Type targetType, object? parameter, string language)
    {
        // Implementation
    }

    /// <inheritdoc/>
    public object ConvertBack(object value, Type targetType, object? parameter, string language)
        => throw new InvalidOperationException("Use forward conversion only.");
}
```

## License

This project is distributed under the MIT License. See [LICENSE](../../LICENSE) for full details.

## References

- [Microsoft WinUI 3 Documentation](https://docs.microsoft.com/en-us/windows/apps/winui/winui3/)
- [XAML Data Binding Documentation](https://docs.microsoft.com/en-us/windows/apps/design/controls/xaml-bindings-overview)
- [IValueConverter Interface](https://docs.microsoft.com/en-us/uwp/api/Windows.UI.Xaml.Data.IValueConverter)
- [DroidNet Mono-Repo README](../README.md)
- [DroidNet Copilot Instructions](./../.github/copilot-instructions.md)
