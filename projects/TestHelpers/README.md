# TestHelpers

A comprehensive .NET testing utility library for the DroidNet platform, providing integrated support for logging, dependency injection, assertion handling, and event handler inspection during unit testing.

## Overview

TestHelpers is a shared testing infrastructure module used across the DroidNet monorepo to standardize test environment setup, reduce boilerplate code, and provide consistent utilities for verifying behavior in unit tests. It integrates industry-standard testing frameworks with DroidNet's preferred dependencies to create a cohesive testing experience.

## Technology Stack

- **Runtime:** .NET 9.0 and .NET 9.0 Windows
- **Testing Framework:** MSTest 4.0
- **Dependency Injection:** DryIoc 6.0 (preview)
- **Logging:** Serilog with Console and Debug sinks
- **Verification:** Verify.MSTest and Verify.DiffPlex
- **Assertion Library:** AwesomeAssertions (via dependent test projects)
- **Reflection & Destructuring:** Destructurama.Attributed

## Project Architecture

TestHelpers is organized as a utility library within the DroidNet monorepo:

```text
TestHelpers/
├── src/
│   ├── CommonTestEnv.cs              # Global test environment initialization
│   ├── TestSuiteWithAssertions.cs    # Base class for assertion-aware test suites
│   ├── DebugAssertUnitTestTraceListener.cs  # Captures Debug.Assert messages
│   ├── EventHandlerTestHelper.cs     # Event handler inspection utilities
│   ├── TestLoggerProvider.cs         # Serilog integration for MSTest
│   ├── SuspendTrackerDisposable.cs   # Suspension tracking utilities
│   ├── TraceListenerCollectionExtensions.cs # TraceListener helpers
│   └── TestHelpers.csproj
├── tests/
│   ├── DebugAssertionTests.cs
│   ├── EventHandlerTestHelperTests.cs
│   ├── TestLoggerProviderTests.cs
│   ├── TraceListenerCollectionExtensionsTests.cs
│   └── TestHelpers.Tests.csproj
└── README.md
```

This library is consumed by all test projects in the DroidNet suite via the `projects/TestHelpers/` module reference.

## Key Features

### 1. Unified Test Environment (`CommonTestEnv`)

Provides a centralized, static test environment with:

- **IoC Container:** DryIoc-backed `TestContainer` for dependency injection across test methods
- **Logging Infrastructure:** Pre-configured Serilog pipeline with dynamic log-level control
- **Cancellation Support:** Access to test context cancellation tokens
- **Verify Framework Base:** Integration with the Verify library for snapshot/approval testing

**Use this** when you need consistent service registration and logging setup across related tests.

### 2. Assertion Capture (`DebugAssertUnitTestTraceListener` & `TestSuiteWithAssertions`)

Intercepts and records `Debug.Assert` failures during tests:

- **DebugAssertUnitTestTraceListener:** A custom `TraceListener` that captures all assertion messages without halting test execution
- **TestSuiteWithAssertions:** Base class that automatically attaches the listener and provides access via `this.TraceListener`

**Use this** when testing code that relies on `Debug.Assert` to verify invariants.

### 3. Event Handler Inspection (`EventHandlerTestHelper`)

Reflection-based utilities to inspect registered event handlers:

- **FindAllRegisteredDelegates():** Lists all delegates for a named event
- **FindRegisteredDelegates():** Filters delegates by target object

**Use this** when verifying that event subscriptions are correctly established or cleaned up.

## Getting Started

### Installation

TestHelpers is available as a NuGet package or as part of the DroidNet source tree:

```powershell
# Via NuGet (if published)
dotnet add package DroidNet.TestHelpers

# Via project reference in monorepo
dotnet add reference projects/TestHelpers/src/TestHelpers.csproj
```

### Basic Setup

Inherit from `CommonTestEnv` or `TestSuiteWithAssertions` in your test class:

```csharp
using DroidNet.TestHelpers;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
public class MyTests : TestSuiteWithAssertions
{
    [TestMethod]
    public void MyTest()
    {
        // CommonTestEnv.TestContainer and CommonTestEnv.LoggingLevelSwitch are available
        // this.TraceListener is available for assertion capture
    }
}
```

## Usage Patterns

### Dynamic Logging Control

Adjust logging verbosity per test without reconfiguration:

```csharp
using Serilog.Events;
using DroidNet.TestHelpers;

[TestMethod]
public void ComplexOperationTest()
{
    // Raise log level for detailed tracing
    CommonTestEnv.LoggingLevelSwitch.MinimumLevel = LogEventLevel.Debug;

    // Run code with verbose logging
    var result = _service.ComplexOperation();

    // Lower level to reduce noise in subsequent tests
    CommonTestEnv.LoggingLevelSwitch.MinimumLevel = LogEventLevel.Warning;

    result.Should().Be(expected);
}
```

### Service Registration and Injection

Use the shared container for consistent test setup:

```csharp
[TestInitialize]
public void SetUp()
{
    var container = CommonTestEnv.TestContainer;
    container.Register<IMyService, MyServiceImplementation>();
    container.Register<IDependency, MockDependency>();
}

[TestMethod]
public void ServiceTest()
{
    var service = CommonTestEnv.TestContainer.Resolve<IMyService>();
    // Test service behavior
}
```

### Capturing Debug Assertions

Verify that code correctly uses `Debug.Assert`:

```csharp
using System.Diagnostics;

[TestMethod]
public void ValidateInvariant_CapturesAssertion_WhenViolated()
{
    // Arrange
    var value = -1; // Invalid state
    this.TraceListener.Clear();

    // Act
    Debug.Assert(value >= 0, "Value must be non-negative");

    // Assert (in DEBUG builds)
#if DEBUG
    this.TraceListener.RecordedMessages.Should().Contain(msg => msg.Contains("non-negative"));
#endif
}
```

### Event Handler Verification

Confirm event subscriptions are managed correctly:

```csharp
[TestMethod]
public void EventSubscription_RegistersAndUnregisters()
{
    // Arrange
    var emitter = new EventEmitter();
    var handler = new EventHandler((s, e) => { });

    // Act - subscribe
    emitter.MyEvent += handler;
    var delegates = EventHandlerTestHelper.FindAllRegisteredDelegates(emitter, "MyEvent");

    // Assert
    delegates.Should().Contain(handler);

    // Act - unsubscribe
    emitter.MyEvent -= handler;
    delegates = EventHandlerTestHelper.FindAllRegisteredDelegates(emitter, "MyEvent");

    // Assert
    delegates.Should().NotContain(handler);
}
```

## Development Workflow

### Building TestHelpers

Build just the TestHelpers project:

```powershell
dotnet build projects/TestHelpers/src/TestHelpers.csproj
```

### Running Tests

Execute the TestHelpers test suite:

```powershell
dotnet test projects/TestHelpers/tests/TestHelpers.Tests.csproj
```

For coverage reporting:

```powershell
dotnet test projects/TestHelpers/tests/TestHelpers.Tests.csproj `
  /p:CollectCoverage=true `
  /p:CoverletOutputFormat=lcov
```

### Generating Solution

To regenerate and open the TestHelpers solution in Visual Studio:

```powershell
cd projects
.\open.cmd TestHelpers
```

Or open the dedicated solution:

```powershell
code projects/TestHelpers/TestHelpers.sln
```

## Coding Standards

TestHelpers adheres to the DroidNet coding conventions:

- **Language:** C# 13 (preview features enabled)
- **Nullable:** Strict null-checking enabled
- **Access Modifiers:** Always explicit (`public`, `private`, `internal`)
- **Instance Members:** Prefixed with `this.`
- **Style:** Follows StyleCop.Analyzers and Roslynator rules
- **Testing:** MSTest with `[TestClass]`, `[TestMethod]`, and `[DataRow]` attributes
- **Patterns:** AAA (Arrange-Act-Assert) with naming `MethodName_Scenario_ExpectedBehavior`

See `.github/instructions/csharp_coding_style.instructions.md` for full details.

## Testing Strategy

TestHelpers itself is tested with comprehensive MSTest suites:

- **DebugAssertionTests.cs:** Validates assertion capture in DEBUG/RELEASE modes
- **EventHandlerTestHelperTests.cs:** Confirms delegate inspection accuracy
- **TestLoggerProviderTests.cs:** Verifies Serilog provider integration
- **TraceListenerCollectionExtensionsTests.cs:** Tests TraceListener extension methods

All tests use AAA pattern and AwesomeAssertions for fluent assertions.

## Integration with DroidNet

TestHelpers is a foundational utility for the DroidNet testing infrastructure:

- **Shared Base:** Used by all test projects in the monorepo (referenced as `projects/TestHelpers/`)
- **MSTest Standardization:** Enforces consistent use of MSTest framework across all modules
- **DI Pattern:** Provides a canonical example of DryIoc integration for test projects
- **Logging Consistency:** Ensures all tests use Serilog with consistent configuration

Refer to the parent `.github/copilot-instructions.md` for testing guidelines across the entire DroidNet platform.

## License

TestHelpers is distributed under the **MIT License**. See the [LICENSE](../../LICENSE) file for details.

## Contributing

When extending TestHelpers:

1. **Keep utilities focused:** Add features only if they benefit multiple test projects across the monorepo
2. **Document patterns:** Provide clear usage examples for new helpers
3. **Write tests:** All new utilities must have corresponding test coverage
4. **Follow conventions:** Adhere to DroidNet C# coding standards and MSTest conventions
5. **Avoid broad APIs:** Coordinate cross-module changes via the DroidNet team

For detailed contribution guidelines, see the root [README.md](../../README.md) and `.github/copilot-instructions.md`.
