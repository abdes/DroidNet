# DroidNet.Routing Router

## Project Description

**DroidNet.Routing.Router** is a sophisticated URL-based routing system for WinUI applications, inspired by Angular's routing architecture. It enables navigation between views using both URL-based routing and dynamic router state manipulation.

The library is designed for .NET 9.0 and integrates seamlessly with WinUI applications, providing a modern and flexible routing solution that maintains familiarity for developers coming from Angular while embracing .NET patterns and practices.

## Technology Stack

- **Platform:** WinUI 3 / .NET 9.0 (Windows 10.0.26100.0+)
- **Language:** C# 13 (with nullable reference types enabled)
- **DI Container:** DryIoc 6.0+
- **Reactive Programming:** System.Reactive (Rx.NET)
- **Logging:** Microsoft.Extensions.Logging.Abstractions
- **Structured Logging:** Destructurama.Attributed
- **Testing:** MSTest 4.0, Moq, Serilog (test diagnostics)
- **Build System:** .NET SDK with centralized package management (`Directory.Packages.props`)

## Project Architecture

### Core Components

The Router module is organized around several key abstractions:

1. **URL Processing Pipeline**
   - `IUrlParser`: Parses URL strings into tree structures
   - `IUrlSerializer`: Serializes router state back to URLs
   - `DefaultUrlParser`: Robust URL parsing with matrix parameters, outlets, and query params
   - `UrlTree`, `UrlSegmentGroup`, `UrlSegment`: Hierarchical URL representation

2. **Route Matching & Resolution**
   - `RouteMatcher`: Matches incoming URLs against configured routes
   - `Recognizer`: Performs route recognition with outlet support
   - `IRouteValidator`: Validates route configurations
   - `DetailRouteActivator`: Handles route activation and lifecycle

3. **Router State Management**
   - `IRouter`: Main router interface for navigation
   - `IRouterStateManager`: Manages router history and state
   - `RouterContextManager`: Manages context for different navigation targets
   - `RouterState`: Represents the current active route tree

4. **Navigation Context**
   - `NavigationContext`: Context data passed during navigation
   - `IContextProvider`: Provides context during route activation
   - Outlet-based context isolation (primary, modal, popup, etc.)

### Architecture Pattern

The router follows an **outlet-based architecture** similar to Angular:

- **Multiple Outlets:** Support for named outlets (primary, modal, popup, etc.)
- **Hierarchical Routes:** Nested route definitions with parent-child relationships
- **Matrix Parameters:** Route-level parameters with syntax like `/path;param=value`
- **Query Parameters:** URL query strings for cross-cutting concerns
- **Lazy Configuration:** Routes and components loaded on demand

## Getting Started

### Installation

Add the NuGet package to your WinUI project:

```bash
dotnet add package DroidNet.Routing.Router
```

Or via the package manager console:

```powershell
Install-Package DroidNet.Routing.Router
```

### Prerequisites

- .NET 9.0 or later
- WinUI 3 application
- Microsoft.Extensions.DependencyInjection or DryIoc container

### Basic Setup

1. Define your routes:

```csharp
var routes = new Routes(
    new Route { Path = "home", Component = typeof(HomeView) },
    new Route { Path = "about", Component = typeof(AboutView) },
    new Route { Path = "documentation", Component = typeof(DocumentationView) }
);
```

1. Configure the router in your DI container:

```csharp
var router = new Router(
    new DefaultUrlParser(),
    new DefaultUrlSerializer(),
    new Recognizer(routes),
    routeActivator,
    contextManager
);
```

1. Navigate using URLs:

```csharp
await router.NavigateByUrl("/home");
```

## Project Structure

```text
Routing.Router/
├── src/
│   ├── Router.cs                      # Main router implementation
│   ├── Route.cs                       # Route definition
│   ├── Routes.cs                      # Route collection
│   ├── IRouter.cs                     # Router interface
│   ├── IUrlParser.cs                  # URL parsing contract
│   ├── DefaultUrlParser.cs            # URL parsing implementation
│   ├── DefaultUrlSerializer.cs        # URL serialization
│   ├── IRouterStateManager.cs         # State management contract
│   ├── RouterStateManager.cs          # State management implementation
│   ├── RouterContextManager.cs        # Context management
│   ├── IRouteValidator.cs             # Route validation contract
│   ├── NavigationContext.cs           # Navigation context data
│   ├── UrlTree.cs                     # URL tree structure
│   ├── UrlSegmentGroup.cs             # URL segment groups
│   ├── UrlSegment.cs                  # URL segments
│   ├── AbstractRouteActivator.cs      # Route activation base
│   ├── Detail/
│   │   ├── Recognizer.cs              # Route recognition
│   │   ├── RouteMatcher.cs            # Route matching logic
│   │   ├── RouterState.cs             # Router state representation
│   │   ├── ActiveRoute.cs             # Active route tracking
│   │   ├── RouteActivationObserver.cs # Activation lifecycle
│   │   └── DefaultRouteValidator.cs   # Route validation
│   └── Utils/
│       ├── RelativeUrlTreeResolver.cs # Relative URL resolution
│       └── ReadOnlyCollectionExtensions.cs
├── tests/
│   ├── RouterTests.cs                 # Router behavior tests
│   ├── MatchTests.cs                  # Route matching tests
│   ├── DefaultUrlParserTests.cs       # URL parser tests
│   ├── DefaultUrlSerializerTests.cs   # URL serializer tests
│   ├── ParametersTests.cs             # Parameter handling tests
│   ├── RouteActivatorTests.cs         # Route activation tests
│   ├── Detail/
│   │   ├── RecognizerTests.cs         # Route recognition tests
│   │   └── DefaultRouteValidatorTests.cs
│   └── Snapshots/                     # Verified test snapshots
└── README.md
```

## Key Features

### 1. URL-Based Navigation

Parse and handle complex URL patterns including:

- **Basic paths:** `/Home`, `/About`
- **Modal routes:** `modal:About` (named outlets)
- **Matrix parameters:** `/Documentation/GettingStarted;toc=true;level=advanced`
- **Nested outlets:** `/Documentation(popup:Feedback)(left:Menu)`
- **Query parameters:** `/Search?query=router&filter=advanced`

### 2. Route Configuration

- **Hierarchical route definitions** with parent-child relationships
- **Multiple outlet support** (primary, modal, popup, custom)
- **Route path matching patterns** (full or prefix-based)
- **ViewModel type association** with routes for MVVM integration
- **Route validation** with comprehensive error reporting

### 3. Router State Management

- **Active route tracking** with reactive state updates
- **Router state serialization** for persistence or debugging
- **Context management** for isolated navigation scopes
- **Router history support** for back/forward navigation (planned)

### 4. Advanced URL Processing

- **Robust URL parsing** with detailed error reporting
- **URL serialization and deserialization** for round-trip fidelity
- **Matrix and query parameter handling** with proper encoding
- **Relative URL resolution** for nested route navigation
- **Outlet-aware URL construction** with multi-outlet support

### 5. Navigation Features

- **Full navigation support** via `NavigateByUrl()`
- **Partial/child route navigation** within nested routes
- **Route activation lifecycle management** with activation/deactivation hooks
- **Navigation events system** for observing route changes
- **Dynamic route configuration** with route registration and modification

## Development Workflow

### Building the Project

Build the specific project:

```powershell
dotnet build projects/Routing/Routing.Router/src/Routing.Router.csproj
```

Or build the Routing solution:

```powershell
cd projects/Routing
dotnet build Routing.sln
```

### Running Tests

Run all tests for Routing.Router:

```powershell
dotnet test projects/Routing/Routing.Router/tests/Routing.Router.Tests.csproj
```

With code coverage:

```powershell
dotnet test projects/Routing/Routing.Router/tests/Routing.Router.Tests.csproj `
  /p:CollectCoverage=true `
  /p:CoverletOutputFormat=lcov
```

### Development Best Practices

- Follow the repository's C# coding style (see `.github/instructions/csharp_coding_style.instructions.md`)
- Use explicit access modifiers (`public`, `private`, `internal`, `protected`)
- Use `this.` prefix for instance members
- Write MSTest-based unit tests with AAA pattern
- Use `Moq` for mocking external dependencies
- Keep test class names following pattern: `MethodName_Scenario_ExpectedBehavior`

## Coding Standards

### C# Style Guide

The project follows the DroidNet repository's strict C# coding standards:

- **Language Features:** C# 13 preview features where appropriate
- **Nullable Reference Types:** Enabled (`<Nullable>enable</Nullable>`)
- **Implicit Usings:** Enabled for standard namespaces
- **Code Analysis:** Strict analyzer rules enforced (StyleCop, Roslynator, Meziantou)
- **Access Modifiers:** Always explicit (no implicit `internal`)
- **Member Prefix:** Use `this.` for all instance member access
- **Naming:** PascalCase for public APIs, camelCase for locals and parameters
- **Documentation:** XML doc comments for all public APIs

### Architecture Principles

- **Composition over inheritance:** Prefer interface-based design
- **Single responsibility:** Each class has one clear purpose
- **Dependency injection:** All dependencies injected via constructor
- **Immutability:** Routes and configuration are immutable
- **Testability:** All components are independently testable with mockable dependencies

## Testing

### Test Framework

The project uses **MSTest 4.0** with the following supporting packages:

- **MSTest:** Core testing framework
- **Moq:** Mocking framework for dependencies
- **Serilog:** Structured logging for test diagnostics
- **AwesomeAssertions:** Enhanced assertion library (via repository setup)

### Test Organization

Tests are organized by component:

- `RouterTests.cs` – Main router behavior and navigation
- `MatchTests.cs` – Route matching engine
- `DefaultUrlParserTests.cs` – URL parsing logic
- `DefaultUrlSerializerTests.cs` – URL serialization
- `ParametersTests.cs` – Matrix and query parameter handling
- `RouteActivatorTests.cs` – Route activation lifecycle
- `Detail/RecognizerTests.cs` – Route recognition with snapshots
- `Detail/DefaultRouteValidatorTests.cs` – Route validation

### Verified Snapshots

Complex route recognition scenarios use **Verified test snapshots** stored in `tests/Detail/Snapshots/`:

```text
RecognizerTests.Recognizer_Match_url=-.verified.txt
RecognizerTests.Recognizer_Match_url=-home.verified.txt
RecognizerTests.Recognizer_Match_url=-project-(left-folders--right-assets)-selected=scenes.verified.txt
```

These snapshots ensure URL parsing results remain consistent across changes.

### Running Tests

Run all tests:

```powershell
dotnet test projects/Routing/Routing.Router/tests/Routing.Router.Tests.csproj
```

Run specific test class:

```powershell
dotnet test projects/Routing/Routing.Router/tests/Routing.Router.Tests.csproj `
  --filter "ClassName=DefaultUrlParserTests"
```

## Contributing

### Before You Start

1. Review this README and the project structure
2. Examine `projects/Routing/Routing.Router/src/` for existing patterns
3. Check `.github/instructions/csharp_coding_style.instructions.md` for code style
4. Look at existing tests in `tests/` to understand the testing patterns

### Making Changes

1. **Small, focused changes:** Keep pull requests limited to a single concern
2. **Follow existing patterns:** Study the codebase before introducing new abstractions
3. **Test coverage:** Add tests for new functionality; maintain or improve coverage
4. **Documentation:** Update XML doc comments and this README if APIs change
5. **No broad API changes:** Public API additions require design discussion

### Code Review Checklist

- [ ] Follows C# coding standards
- [ ] All public APIs have XML documentation
- [ ] Tests added for new functionality
- [ ] Tests pass locally
- [ ] No breaking changes to public APIs
- [ ] Architecture remains clean and maintainable

## License

This project is part of the DroidNet mono-repo and is distributed under the **MIT License**. See the LICENSE file in the repository root for details.

---

**Related Documentation:**

- Main DroidNet repository: [GitHub - DroidNet](https://github.com/abdes/DroidNet)
- Repository copilot instructions: `.github/copilot-instructions.md`
- C# coding standards: `.github/instructions/csharp_coding_style.instructions.md`
- Routing module overview: `projects/Routing/`
