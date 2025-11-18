# Hosting (DroidNet.Hosting)

## Project Name and Description

The `Hosting` module (DroidNet.Hosting) integrates the .NET Generic Host with WinUI 3 so desktop
applications can leverage the host's dependency injection, configuration, logging, and lifetime
management. Rather than starting the WinUI dispatcher directly from `Main`, this module runs the
WinUI dispatcher as a hosted background service — enabling the UI and other hosted services to
share a single managed runtime and predictable application lifecycle.

Key API elements:

- `UserInterfaceHostedService` (see `WinUI/UserInterfaceHostedService.cs`) — runs the UI dispatcher
    and coordinates hosted services with the UI lifecycle.
- `HostingContext` (see `WinUI/HostingContext.cs`) — configuration and options for UI-host linking
    and behavior.

The `samples/DemoApp` provides a recommended setup and example registrations.

## Technology Stack

- **Framework:** .NET 9 (Windows target: `net9.0-windows10.0.26100.0`)
- **UI Framework:** WinUI 3 (Microsoft.WindowsAppSDK 1.8+)
- **Hosting / DI:** Microsoft.Extensions.Hosting / Microsoft.Extensions.DependencyInjection (DryIoc
    integration is commonly used in DroidNet solutions)
- **Reactive:** System.Reactive for reactive scheduling and coordination
- **Logging:** Serilog (or other Microsoft.Extensions.Logging-compatible providers)
- **Build Tools:** Microsoft.Windows.SDK.BuildTools
- **Testing:** MSTest for unit testing

## Key Features

- Unified lifetime management: Host UI and background services together and control how closing
    either side affects application lifetime
- Built-in configuration support: Uses host configuration sources (appsettings.*, env vars,
    command-line args, user secrets)
- Structured logging: Integrates with Serilog or any Microsoft.Extensions.Logging provider
- Extensible DI registration: Register UI components (views, view models, services) via standard
    host DI patterns
- Reactive dispatch scheduling: `DispatcherQueueScheduler` for scheduling work on the WinUI
    dispatcher using System.Reactive

## Project Architecture

### Core Concepts

At the center is a `UserInterfaceHostedService`: a hosted service that starts and manages a WinUI
dispatcher thread and integrates the dispatcher with the host's lifecycle. This gives a single
managed host process for UI and background services and enables predictable shutdown and startup
behaviors.

Core responsibilities:

1. Dependency Injection: Normal host DI pattern ensures services and UI components are resolved via
    the container.
2. Configuration Management: Uses host configuration (appsettings.json, environment, CLI, secrets).
3. Structured Logging: Integrates with Microsoft.Extensions.Logging/Serilog at host level.
4. Service Lifetime Management: Links or decouples the UI and host lifecycles based on configuration
    options.
5. Background Service Coordination: UI runs together with other hosted services and coordinates
    graceful shutdown.

### Module Structure

```text
Hosting/
├── src/
│   ├── Hosting.csproj              # Main library project
│   ├── BaseHostingContext.cs       # Base context for hosting options
│   ├── BaseUserInterfaceThread.cs  # Base class for UI thread implementations
│   ├── IUserInterfaceThread.cs     # Interface for UI thread contract
│   └── WinUI/
│       ├── HostingContext.cs       # WinUI-specific hosting context
│       ├── UserInterfaceHostedService.cs  # IHostedService implementation
│       ├── UserInterfaceThread.cs  # WinUI dispatcher thread wrapper
│       ├── DispatcherQueueScheduler.cs    # Reactive scheduler for dispatcher
│       └── WinUiHostingExtensions.cs     # Extension methods for host builder
├── samples/
│   └── DemoApp/                    # Reference implementation
├── tests/
│   └── *.Tests.csproj              # Unit tests
└── README.md
```

### Architecture Diagram (Conceptual)

```text
┌─────────────────────────────────────────────┐
│      Microsoft.Extensions.Host              │
│  (DI, Configuration, Logging, Lifetime)     │
├─────────────────────────────────────────────┤
│                                             │
│  ┌─────────────────────────────────────┐    │
│  │  UserInterfaceHostedService         │    │
│  │  (IHostedService impl)              │    │
│  ├─────────────────────────────────────┤    │
│  │                                     │    │
│  │  UserInterfaceThread                │    │
│  │  (WinUI DispatcherQueue)            │    │
│  │                                     │    │
│  │  ┌──────────────────────────────┐   │    │
│  │  │  Application + MainWindow    │   │    │
│  │  │  (Managed by app code)       │   │    │
│  │  └──────────────────────────────┘   │    │
│  │                                     │    │
│  └─────────────────────────────────────┘    │
│                                             │
│  + Other Hosted Services                    │
│                                             │
└─────────────────────────────────────────────┘
```

## Getting Started

### Quick Start

1. Clone the DroidNet repo and run the repo initialization script once:

```powershell
pwsh ./init.ps1
```

1. Open the Hosting solution or the full repo solution:

```powershell
cd projects
.\open.cmd
```

1. Build and run the `samples/DemoApp` to see the hosting model in action.

### Prerequisites

- .NET 9 SDK or later
- Windows 10 (build 26100+) or Windows 11
- Visual Studio 2022 (v17.8+) or another .NET-capable IDE

### Installation

Reference the project directly (during development):

```pwsh
dotnet add reference projects/Hosting/src/Hosting/Hosting.csproj
```

Or add the package when it is published to NuGet:

```pwsh
dotnet add package DroidNet.Hosting
```

### Basic Setup

Use the Generic Host to configure and run the WinUI runtime as a hosted service:

```csharp
[STAThread]
private static void Main(string[] args)
{
    var builder = Host.CreateApplicationBuilder(args);

    // Configure your services
    builder.Services.AddSingleton<IMyService, MyService>();

    // Configure WinUI hosting
    builder.ConfigureServices(sc => sc.ConfigureWinUI<App>(isLifetimeLinked: true));

    var host = builder.Build();
    host.Run();
}
```

Make sure `App.xaml.cs` remains a partial class without its own `Main` override if using the
hosted setup.

### Configuration

The host builder aggregates configuration automatically from common sources:

- `appsettings.json` and `appsettings.{Environment}.json`
- Environment variables
- Command-line arguments
- User secrets in development

Use `IConfiguration` in services and view models to read settings.

### Dependency Injection

Register services using standard host DI patterns:

```csharp
builder.Services.AddSingleton<IMyService, MyService>();
builder.Services.AddTransient<IRepository, Repository>();
builder.Services.AddScoped<IUnitOfWork, UnitOfWork>();
```

Inject dependencies into `ViewModels`, `Services`, or `Views` where needed.

## Development Workflow

- Run `init.ps1` once after cloning to set up SDKs and tools.
- Prefer building and testing individual projects during development to speed iteration:

```pwsh
dotnet build projects/Hosting/src/Hosting/Hosting.csproj
dotnet test --project projects/Hosting/tests/Hosting.Tests.csproj
```

- Regenerate and open the solution when project files change with `projects\\open.cmd`.
- Clean build artifacts with `./clean.ps1`.
- Use feature branches and open a PR for changes. Include tests and follow the repository's PR
    template and guidelines.

## Coding Standards

- Language & C# style: Follow the repo's C# conventions — explicit access modifiers, `this.` for instance members,
    nullable reference types enabled, implicit usings where appropriate.
- Linters & analyzers: Address StyleCop, Roslynator, Meziantou and other analyzer warnings.
- Patterns: Reuse source-generator patterns (e.g., `projects/Mvvm.Generators`) and keep public API surfaces
    minimal and well-documented.

## Testing

- Framework: MSTest for unit tests. UI tests (visual) use `.UI.Tests` naming where applicable.
- Test pattern & naming: Use AAA (Arrange-Act-Assert) and name tests `MethodName_Scenario_ExpectedBehavior`.
- Run tests locally (project level):

```pwsh
dotnet test --project projects/Hosting/tests/Hosting.Tests.csproj
```

- Test helpers are in `projects/TestHelpers/`.

## Contributing

Contributions are welcome. A few guidelines to follow:

- Use a branch per feature/fix (e.g., `feature/my-fix`).
- Run `init.ps1` once after cloning.
- Build, run unit tests and fix analyzer warnings before opening a PR.
- Add unit tests for new behavior and update existing ones if behavior changes.
- If you plan to change repo-wide rules (formatting, analysis), open an issue first.

See the root repository `CONTRIBUTING.md` (if present) for full contribution rules.

## License

See the `LICENSE` file in the repository root for license details.
