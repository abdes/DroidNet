# DroidNet Application Bootstrapper

A reusable bootstrapping library for WinUI applications that provides a fluent configuration API for setting up dependency injection, logging, routing, MVVM, and other application services.

## Features

- Fluent configuration API for clean and maintainable startup code
- Built-in support for:
  - Dependency injection (using DryIoc)
  - Configuration (JSON files with environment-specific settings)
  - Logging (using Serilog)
  - MVVM infrastructure
  - Routing
  - WinUI integration

## Installation

```xml
<PackageReference Include="DroidNet.Bootstrap" Version="1.0.0" />
```

## Quick Start

```csharp
// In your application entry point
var bootstrap = new Bootstrapper(args)
    .Configure()                    // Basic initialization
    .WithWinUI<App>()               // Setup WinUI integration
    .WithConfiguration(             // Configure settings
        GetConfigFiles,
        ConfigureOptions)
    .WithLoggingAbstraction()       // Setup logging
    .WithMvvm()                     // Setup MVVM support
    .WithRouting(routes)            // Setup routing
    .Build();                       // Build the application host

bootstrap.Run();                    // Start the application
```

## Configuration Methods

- `Configure()` : Initial setup of basic services
- `WithWinUI<TApplication>()` : Configures WinUI hosting and integration
- `WithConfiguration()` : Sets up JSON configuration files and options
- `WithLoggingAbstraction()` : Configures Serilog logging infrastructure
- `WithMvvm()` : Registers view location and view model binding services
- `WithRouting()` : Configures application routing
- `WithAppServices()` : Configures custom application services
- `Build()` : Finalizes configuration and builds the application host
- `Run()` : Starts the application

## Example Usage

```csharp
var bootstrap = new Bootstrapper(args)
    .Configure()
    .WithWinUI<App>()
    .WithConfiguration(
        (finder, fs, config) => new[]
        {
            "appsettings.json",
            $"appsettings.{config["mode"]}.json"
        },
        (config, services) =>
        {
            services.Configure<AppSettings>(
                config.GetSection("AppSettings"));
        })
    .WithLoggingAbstraction()
    .WithMvvm()
    .WithRouting(new Routes())
    .WithAppServices(container =>
    {
        container.Register<IMyService, MyService>(Reuse.Singleton);
    })
    .Build();

bootstrap.Run();
```

## Lifecycle

1. Basic initialization via `Configure()`
2. Service configuration through fluent methods
3. Application startup with `Build()` and `Run()`

## Best Practices

- Call `Configure()` first before any other configuration methods
- Configure WinUI integration before other services
- Dispose the bootstrapper when the application exits
- Use dependency injection for service resolution
- Configure logging early in the startup sequence
