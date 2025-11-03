# DroidNet.Coordinates

[![NuGet](https://img.shields.io/nuget/v/DroidNet.Coordinates?style=flat-square)](https://www.nuget.org/packages/DroidNet.Coordinates/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=flat-square)](https://opensource.org/licenses/MIT)

> Type-safe spatial coordinate transformations for WinUI 3 applications

**DroidNet.Coordinates** is a compile-time safe spatial mapping library for
WinUI 3 that abstracts coordinate transformations across element, window, and
screen spaces. With full DPI awareness and multi-monitor support built-in, it
correctly handles coordinate transformations across monitors with different
scaling factors, providing generic spatial point types tagged by coordinate
space, fluent conversion APIs, and context-aware services that integrate cleanly
with dependency injection.

## Features

- **Type Safety**: Generic spatial points prevent mixing coordinates from different spaces at compile time
- **Multi-Space Support**: Seamlessly convert between element, window, and screen coordinate systems
- **Multi-Monitor Aware**: Correctly handles coordinate transformations across monitors with different DPI scaling factors
- **Per-Monitor DPI**: Automatically detects and applies the correct DPI for each monitor when converting to screen coordinates
- **Fluent API**: Chain coordinate transformations and operations with an intuitive fluent interface
- **Dependency Injection**: First-class support for DI with factory delegates and context services
- **Zero Runtime Overhead**: Type parameters are erased at runtime - no performance penalty

## Installation

```powershell
dotnet add package DroidNet.Coordinates
```

Or via NuGet Package Manager:

```powershell
Install-Package DroidNet.Coordinates
```

## Quick Start

### Basic Usage

```csharp
using DroidNet.Coordinates;
using Microsoft.UI.Xaml;
using Windows.Foundation;

// Create spatial points in different coordinate spaces
var elementPoint = new SpatialPoint<ElementSpace>(new Point(10, 20));
var windowPoint = new SpatialPoint<WindowSpace>(new Point(100, 200));
var screenPoint = new SpatialPoint<ScreenSpace>(new Point(500, 400));

// Or use extension methods for concise syntax
var point = new Point(10, 20);
var asElement = point.AsElement();
var asWindow = point.AsWindow();
var asScreen = point.AsScreen();
```

### Coordinate Transformations

```csharp
// Create a mapper for coordinate transformations
var mapper = new SpatialMapper(frameworkElement, window);

// Convert between coordinate spaces
var screenPoint = mapper.ToScreen(elementPoint);
var windowPoint = mapper.ToWindow(screenPoint);
var backToElement = mapper.ToElement(windowPoint);

// Generic conversion
var converted = mapper.Convert<ElementSpace, ScreenSpace>(elementPoint);
```

### Fluent API

```csharp
// Chain transformations with the fluent API
var result = new Point(10, 20)
    .AsElement()
    .Flow(mapper)
    .ToScreen()
    .OffsetX(50)
    .OffsetY(25)
    .ToWindow()
    .ToPoint();

// Complex transformations
var point = elementPoint
    .Flow(mapper)
    .ToScreen()
    .Offset(10, 20)
    .ToWindow()
    .OffsetBy(new SpatialPoint<WindowSpace>(new Point(5, 5)))
    .ToElement()
    .Point;
```

### Spatial Arithmetic

```csharp
// Spatial points support arithmetic operations (same space only)
var a = new SpatialPoint<ElementSpace>(new Point(10, 20));
var b = new SpatialPoint<ElementSpace>(new Point(5, 10));

var sum = a + b;        // (15, 30)
var difference = a - b; // (5, 10)

// Or use static methods
var sum2 = SpatialPoint.Add(a, b);
var diff2 = SpatialPoint.Subtract(a, b);

// Mixing spaces won't compile - type safety!
// var invalid = elementPoint + windowPoint; // Compile error!
```

### Dependency Injection

The recommended way to use DroidNet.Coordinates with dependency injection is to
register the `SpatialMapperFactory` delegate and the `SpatialContextService`:

```csharp
using DryIoc;

// Register the factory delegate that creates mappers
container.RegisterDelegate<SpatialMapperFactory>(r =>
    (element, window) => new SpatialMapper(element, window));

// Register the context service
container.Register<SpatialContextService>();
```

Then consume the service in your classes:

```csharp
public class MyViewModel
{
    private readonly SpatialContextService spatialContext;

    public MyViewModel(SpatialContextService spatialContext)
    {
        this.spatialContext = spatialContext;
    }

    public void HandleElementInteraction(FrameworkElement element, Window window)
    {
        // Get a mapper for this specific element and window
        var mapper = this.spatialContext.GetMapper(element, window);

        // Use the mapper for coordinate transformations
        var screenPoint = mapper.ToScreen(elementPoint);
    }
}
```

For scenarios where mapper creation is expensive or may not be needed
immediately, use lazy initialization:

```csharp
public class MyControl
{
    private readonly SpatialContextService spatialContext;
    private Lazy<ISpatialMapper>? lazyMapper;

    public MyControl(SpatialContextService spatialContext)
    {
        this.spatialContext = spatialContext;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        // Create lazy mapper - actual mapper won't be created until accessed
        this.lazyMapper = this.spatialContext.GetLazyMapper(this, this.XamlRoot.Content.XamlRoot);
    }

    private void OnPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        // Mapper is created only on first access
        var mapper = this.lazyMapper!.Value;
        var position = e.GetCurrentPoint(this).Position;
        var screenPos = mapper.ToScreen(position.AsElement());
    }
}
```

> [!NOTE] Each call to `GetMapper()` returns a new `SpatialMapper` instance. The
> factory pattern allows you to create context-specific mappers for different
> element/window combinations as needed.

## Core Concepts

### Coordinate Spaces

DroidNet.Coordinates defines three coordinate spaces:

- **`ElementSpace`**: Coordinates relative to a UI element's top-left corner
- **`WindowSpace`**: Coordinates relative to the application window's client area
- **`ScreenSpace`**: Absolute screen coordinates (physical pixels)

Each space is represented by a marker type that tags spatial points at compile time, preventing accidental mixing of coordinate systems.

### Spatial Points

`SpatialPoint<TSpace>` wraps a `Windows.Foundation.Point` with a type parameter indicating its coordinate space:

```csharp
public readonly record struct SpatialPoint<TSpace>(Point Point)
{
    public static SpatialPoint<TSpace> operator +(SpatialPoint<TSpace> a, SpatialPoint<TSpace> b);
    public static SpatialPoint<TSpace> operator -(SpatialPoint<TSpace> a, SpatialPoint<TSpace> b);
    public SpatialFlow<TSpace> Flow(ISpatialMapper mapper);
}
```

### Spatial Mapper

The `SpatialMapper` class handles all coordinate transformations, accounting for:

- Element positions within the window
- Window decorations and title bar
- Per-monitor DPI scaling factors
- Multi-monitor configurations with different scaling settings
- Physical to logical pixel conversions

```csharp
public class SpatialMapper(FrameworkElement element, Window window) : ISpatialMapper
{
    public SpatialPoint<TTarget> Convert<TSource, TTarget>(SpatialPoint<TSource> point);
    public SpatialPoint<ScreenSpace> ToScreen<TSource>(SpatialPoint<TSource> point);
    public SpatialPoint<WindowSpace> ToWindow<TSource>(SpatialPoint<TSource> point);
    public SpatialPoint<ElementSpace> ToElement<TSource>(SpatialPoint<TSource> point);
}
```

### Spatial Flow

`SpatialFlow<TSpace>` provides a fluent interface for chaining transformations and offsets:

```csharp
public class SpatialFlow<TSpace>
{
    public SpatialFlow<ScreenSpace> ToScreen();
    public SpatialFlow<WindowSpace> ToWindow();
    public SpatialFlow<ElementSpace> ToElement();
    public SpatialFlow<TSpace> Offset(double dx, double dy);
    public SpatialFlow<TSpace> OffsetX(double dx);
    public SpatialFlow<TSpace> OffsetY(double dy);
    public SpatialFlow<TSpace> OffsetBy(SpatialPoint<TSpace> offset);
    public Point ToPoint();
}
```

## Use Cases

DroidNet.Coordinates is designed for precision-critical scenarios:

- **Input Simulation**: Convert UI element coordinates to screen coordinates for automated input
- **Layout Validation**: Verify element positions across different coordinate systems in tests
- **UI Automation**: Precisely locate and interact with UI elements in testing frameworks
- **Custom Controls**: Build controls that need to work with multiple coordinate systems
- **Multi-Monitor Apps**: Handle coordinate transformations across monitors with different DPI settings
- **Drag and Drop**: Calculate precise drop positions when dragging across windows and monitors

## Example: UI Testing

```csharp
// In a UI test, locate an element and simulate a click
var button = FindButton("Submit");
var mapper = new SpatialMapper(button, mainWindow);

// Get the center of the button in screen coordinates
var buttonCenter = new Point(button.ActualWidth / 2, button.ActualHeight / 2)
    .AsElement()
    .Flow(mapper)
    .ToScreen()
    .ToPoint();

// Use the screen coordinates for input simulation
SimulateMouseClick(buttonCenter);
```

## Requirements

- .NET 9.0 or later
- Windows 10.0.26100.0 or later (Windows App SDK)
- WinUI 3

## Contributing

This project is part of the [DroidNet](https://github.com/abdes/DroidNet) mono-repo. Contributions are welcome! Please ensure your changes include appropriate tests and follow the existing code style.

## License

This project is licensed under the MIT License - see the [LICENSE](../../LICENSE) file for details.

## Resources

- [WinUI 3 Documentation](https://learn.microsoft.com/windows/apps/winui/)
- [Windows App SDK](https://learn.microsoft.com/windows/apps/windows-app-sdk/)
- [DroidNet Project](https://github.com/abdes/DroidNet)
