# DroidNet Converters

## Overview

The Converters project is a collection of value converters for use in .NET applications, specifically targeting the .NET 8 framework. These converters are designed to facilitate the binding of data to UI elements by converting values from one type to another.

## Available Converters

- **BoolToBrushConverter**: Selects a color Brush based on a bool value.
- **NullToVisibilityConverter**: Converts a null value to a Visibility value. This converter is useful for scenarios where you need to show or hide UI elements based on the presence or absence of a value.
- **ItemClickEventArgsToClickedItemConverter**: Converts an ItemClickEventArgs object into the corresponding clicked item object.
- **IndexToNavigationItemConverter**: Gets the navigation item corresponding to the given index in the containing NavigationView.
- **DictionaryValueConverter<T>**: Converts a binding to a dictionary with a string key as a parameter to the value corresponding to that key in the dictionary.

## Installation

### Using dotnet CLI

To install the DroidNet.Converters package, run the following command:
```
dotnet add package DroidNet.Converters
```

### Using project reference
Add a reference to the DroidNet.Converters project in your .csproj file:
```xml
<ItemGroup>
    <ProjectReference Include="..\path\to\Converters.csproj" />
</ItemGroup>
```

### XAML import
Add the namespace to your XAML files:
```xaml
xmlns:converters="using:DroidNet.Converters"
```
## Sample Usage

```xml
<converters:BoolToBrushConverter x:Key="BoolToBrush"
    ActiveBrush="Red"
    InactiveBrush="Transparent"/>

<Rectangle Fill="{Binding IsActive, Converter={StaticResource BoolToBrush}}"/>
```
