# DroidNet Controls Helpers

The `Controls.Helpers` library provides essential helper classes and utilities to support various UI controls and components in .NET desktop applications, particularly those built using WinUI 3.

## Key Features

- **Selection Models**: A set of abstract base classes (`SingleSelectionModel<T>` and `MultipleSelectionModel<T>`) for managing selection states in UI controls like lists, trees, and grids. These models enforce specific selection rules (single/multiple) and provide a consistent API for working with selected items.
- **SelectionObservableCollection<T>**: A custom observable collection that manages the `IsSelected` property of its items when they are added or removed, ensuring that selected items remain in sync with the underlying data model.

## Usage

1. First, install the `DroidNet.Controls.Helpers` package via NuGet:
   ```shell
   dotnet add DroidNet.Controls.Helpers
   ```

2. Import the required namespaces in your code or XAML file:

   ```csharp
   using DroidNet.Controls;
   ```

## Samples & Examples

Explore the following sample projects for working examples demonstrating various features and use cases of the `Controls.Helpers` library:

- [DynamicTree Control](../DynamicTree/README.md): Demonstrates using the `SelectionObservableCollection<T>` and custom selection models to manage selected items in a dynamic tree view control.

*Happy coding! 🚀*
