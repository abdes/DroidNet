# Aura

Aura is a .NET project designed to provide a rich user interface experience with customizable themes, dynamic menus, and enhanced window decorations. It leverages modern .NET technologies and targets .NET 8 and .NET Standard 2.0.

## Features

- **Customizable Themes**: Easily switch between light, dark, and system default themes.
- **Dynamic Menus**: Build and manage menus with support for flyouts and menu bars.
- **Enhanced Window Decorations**: Custom title bars, icons, and window content management.
- **MVVM Architecture**: Utilizes the MVVM pattern for clean separation of concerns.

## Getting Started

### Prerequisites

- .NET 8 SDK
- Visual Studio 2022 or later

### Installation

1. Clone the repository:
```sh
git clone https://github.com/yourusername/aura.git
```

2. Navigate to the project directory:
```sh
cd aura
```

3. Open the solution in Visual Studio:
```sh
start Aura.sln
```

### Building the Project

To build the project, open the solution in Visual Studio and build the solution using the Build menu or by pressing `Ctrl+Shift+B`.

### Running the Project

To run the project, set the desired startup project in Visual Studio and press `F5` or use the Debug menu to start debugging.

## Usage

### Customizing Themes

You can customize the application theme by using the `IAppThemeModeService` interface. Here is an example of how to apply a theme:

```csharp
// Assuming 'window' is a valid Window instance and 'themeService' is an instance of IAppThemeModeService
themeService.ApplyThemeMode(window, ElementTheme.Dark);
```

### Building Menus

Use the `MenuBuilder` class to create and manage menus. Here is an example of how to build a menu flyout:

```csharp
var menuBuilder = new MenuBuilder();
menuBuilder.AddMenuItem(new MenuItem
{
    Text = "Settings",
    Command = new RelayCommand(() => ShowSettings())
});
var menuFlyout = menuBuilder.BuildMenuFlyout();
```

### Enhanced Window Decorations

The `MainShellView` class provides enhanced window decorations, including a custom title bar and dynamic content management. Here is an example of how to set up the custom title bar:

```csharp
this.CustomTitleBar.Loaded += (_, _) => this.SetupCustomTitleBar();
this.CustomTitleBar.SizeChanged += (_, _) => this.SetupCustomTitleBar();
```
