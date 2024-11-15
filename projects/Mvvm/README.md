# DroidNet MVVM Extensions

The DroidNet.Mvvm project provides advanced MVVM (Model-View-ViewModel) capabilities for .NET/WinUI applications, offering robust tools for managing the separation between UI and business logic.

## Key Features

1. **MVVM Infrastructure**
   - Generic `IViewFor<T>` interface for strong typing between Views and ViewModels
   - Support for ViewModel change notifications via `ViewModelChanged` event
   - Clean separation between View and ViewModel layers

2. **Intelligent View Resolution**
   - Automatic View discovery using naming conventions
   - Flexible view resolution strategies:
     - Direct `IViewFor<T>` resolution
     - Name-based matching (e.g., "ViewModel" → "View")
     - Interface/class name toggle resolution (e.g., "IMyView" ↔ "MyView")
   - Customizable naming conventions via `ViewModelToViewFunc`

3. **WinUI Integration**
   - XAML-friendly `ViewModelToView` converter
   - Seamless binding support in XAML templates
   - Example usage:
     ```xaml
     <ContentPresenter Content="{x:Bind ViewModel.Workspace,
         Converter={StaticResource VmToViewConverter}}" />
     ```

4. **Dependency Injection**
   - Built-in DryIoc container support
   - Extensible service resolution
   - Logging integration via `ILoggerFactory`

## Setup

Register the view locator:
```cs
services.AddSingleton<IViewLocator, DefaultViewLocator>();
```

Register your views:
```cs
services.AddTransient<IViewFor<MyViewModel>, MyView>();
```

Configure the converter in App.xaml.cs:
```cs
public App([FromKeyedServices("Default")] IValueConverter vmToViewConverter)
{
    Resources["VmToViewConverter"] = vmToViewConverter;
}
```

## Best Practices

1. Implement `IViewFor<T>` on your views to ensure proper type safety
2. Use interface-based view models for better abstraction
3. Follow naming conventions for automatic view resolution
4. Leverage the logging system for troubleshooting resolution failures
