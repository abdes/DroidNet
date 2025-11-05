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

Register DroidNet.Coordinates with DryIoc using the provided extension method.
It wires up the transient mapper factory and exposes a strongly typed
`SpatialMapperFactory` delegate.

```csharp
using DryIoc;
using DroidNet.Coordinates;

var container = new Container().WithSpatialMapping();

// Chaining additional registrations is supported
container.Register<MyViewModel>();
```

Inject the `SpatialMapperFactory` delegate wherever you need to produce mappers at runtime:

```csharp
/// <summary>Strongly typed factory delegate for creating ISpatialMapper instances.</summary>
public delegate ISpatialMapper SpatialMapperFactory(FrameworkElement element, Window? window);

public class MyViewModel
{
    private readonly SpatialMapperFactory spatialMapperFactory;

    public MyViewModel(SpatialMapperFactory spatialMapperFactory)
    {
        this.spatialMapperFactory = spatialMapperFactory;
    }

    public void HandleElementInteraction(FrameworkElement element, Window window)
    {
        var mapper = this.spatialMapperFactory(element, window);
        var screenPoint = mapper.ToScreen(elementPoint);
    }
}
```

The `window` parameter may be `null` if only element-space conversions are
needed. Each invocation of `SpatialMapperFactory` yields a new mapper instance,
keeping element/window pairs isolated and letting you manage lifetime and
caching semantics explicitly.

## Core Concepts

### Coordinate Spaces

DroidNet.Coordinates defines four coordinate spaces:

- **`ElementSpace`**: Coordinates relative to a UI element's top-left corner (logical DIPs)
- **`WindowSpace`**: Coordinates relative to the application window's client area (logical DIPs)
- **`ScreenSpace`**: Absolute desktop-global screen coordinates in logical pixels (DIPs), with per-monitor DPI awareness
- **`PhysicalScreenSpace`**: Absolute screen coordinates in physical device pixels (hardware pixels) for Win32 interop only

Each space is represented by a marker type that tags spatial points at compile
time, preventing accidental mixing of coordinate systems.

**Units:**

- Logical spaces (Element, Window, Screen) use logical pixels (DIPs, 1 DIP =
  1/96 inch), desktop-virtualized with per-monitor DPI awareness
- `PhysicalScreenSpace` uses physical pixels (px) for direct Win32 API interoperability

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
- Logical to physical pixel conversions for Win32 interop

```csharp
public interface ISpatialMapper
{
    public WindowInfo WindowInfo { get; }
    public WindowMonitorInfo WindowMonitorInfo { get; }

    public SpatialPoint<TTarget> Convert<TSource, TTarget>(SpatialPoint<TSource> point);
    public SpatialPoint<ScreenSpace> ToScreen<TSource>(SpatialPoint<TSource> point);
    public SpatialPoint<WindowSpace> ToWindow<TSource>(SpatialPoint<TSource> point);
    public SpatialPoint<ElementSpace> ToElement<TSource>(SpatialPoint<TSource> point);
    public SpatialPoint<PhysicalScreenSpace> ToPhysicalScreen<TSource>(SpatialPoint<TSource> point);
}
```

**WindowInfo**: Provides aggregated information about the mapper's window
including client origin/size and outer window bounds in logical DIPs, plus the
window's DPI.

**WindowMonitorInfo**: Provides information about the monitor containing the
window, including physical dimensions (pixels), logical dimensions (DIPs), and
monitor DPI. Includes an `IsValid` property to check if monitor detection
succeeded.

### Spatial Flow

`SpatialFlow<TSpace>` provides a fluent interface for chaining transformations and offsets:

```csharp
public class SpatialFlow<TSpace>
{
    public SpatialPoint<TSpace> Point { get; }

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

Access the current `Point` property to get the underlying `SpatialPoint<TSpace>` at any stage in the flow chain.

## Use Cases

DroidNet.Coordinates is designed for precision-critical scenarios:

- **Input Simulation**: Convert UI element coordinates to screen coordinates for automated input
- **Layout Validation**: Verify element positions across different coordinate systems in tests
- **UI Automation**: Precisely locate and interact with UI elements in testing frameworks
- **Custom Controls**: Build controls that need to work with multiple coordinate systems
- **Multi-Monitor Apps**: Handle coordinate transformations across monitors with different DPI settings
- **Drag and Drop**: Calculate precise drop positions when dragging across windows and monitors
- **Win32 Interop**: Convert logical WinUI coordinates to physical screen pixels for Win32 APIs

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

## Example: Win32 Interop

When working with Win32 APIs that expect physical screen coordinates (e.g.,
`GetCursorPos`, `SetWindowPos`), convert to `PhysicalScreenSpace`:

```csharp
// Convert WinUI element coordinates to physical screen pixels for Win32 API
var mapper = new SpatialMapper(targetElement, window);

// Get element center in element space, convert to physical screen space
var elementCenter = new Point(targetElement.ActualWidth / 2, targetElement.ActualHeight / 2)
    .AsElement()
    .Flow(mapper)
    .ToPhysicalScreen()
    .ToPoint();

// Round to integers for Win32 APIs
var physicalX = (int)Math.Round(elementCenter.X);
var physicalY = (int)Math.Round(elementCenter.Y);

// Use with Win32 APIs
var point = new Windows.Win32.Foundation.POINT { X = physicalX, Y = physicalY };
PInvoke.SetCursorPos(physicalX, physicalY);
```

**Key Differences:**

- **`ScreenSpace`**: Use for UI math and calculations within your WinUI app.
  Coordinates are in logical DIPs, desktop-virtualized.
- **`PhysicalScreenSpace`**: Use **only** at Win32 API boundaries. Coordinates
  are in physical device pixels. Always round to integers before passing to
  Win32 functions.

## Requirements

- .NET 9.0 or later
- Windows 10.0.26100.0 or later (Windows App SDK)
- WinUI 3

## Contributing

This project is part of the [DroidNet](https://github.com/abdes/DroidNet)
mono-repo. Contributions are welcome! Please ensure your changes include
appropriate tests and follow the existing code style.

## License

This project is licensed under the MIT License - see the [LICENSE](../../LICENSE) file for details.

## Resources

- [WinUI 3 Documentation](https://learn.microsoft.com/windows/apps/winui/)
- [Windows App SDK](https://learn.microsoft.com/windows/apps/windows-app-sdk/)
- [DroidNet Project](https://github.com/abdes/DroidNet)
