# Test Helpers

TestHelpers is a .NET library designed to assist with unit testing by providing utilities for logging, dependency injection, assertion handling, and event handler testing. This library includes tools to enhance the testing experience and ensure comprehensive test coverage.

## Features

- **Logging**: Configures Serilog for detailed logging during tests.
- **Dependency Injection**: Utilizes DryIoc for managing dependencies in tests.
- **Assertion Handling**: Provides a custom TraceListener to capture assertion failures.
- **Event Handler Testing**: Includes helpers to check if specific event handlers are registered/unregistered.

## Installation

To install the TestHelpers library, you can use the NuGet Package Manager:

```sh
dotnet add package TestHelpers
```

## Usage

### Logging

The `CommonTestEnv` class configures Serilog for logging during tests. It provides a `LoggingLevelSwitch` to dynamically change the logging level.

#### Example

```csharp
using Serilog.Events;
using DroidNet.TestHelpers;

CommonTestEnv.LoggingLevelSwitch.MinimumLevel = LogEventLevel.Debug;
// Your test code here
CommonTestEnv.LoggingLevelSwitch.MinimumLevel = LogEventLevel.Warning;
```

### Dependency Injection

The `CommonTestEnv` class uses DryIoc for dependency injection. It provides a static `TestContainer` property to manage dependencies.

#### Example

```csharp
using DryIoc;
using DroidNet.TestHelpers;

var container = CommonTestEnv.TestContainer;
container.Register<ISomeService, SomeService>();
var service = container.Resolve<ISomeService>();
```

### Assertion Handling

The `DebugAssertUnitTestTraceListener` class captures assertion failures during tests. It records messages and provides methods to access and clear them.

#### Example

```csharp
using System.Diagnostics;
using DroidNet.TestHelpers;

var listener = new DebugAssertUnitTestTraceListener();
Debug.Listeners.Add(listener);

// Your test code that may trigger assertions

var messages = listener.RecordedMessages;
listener.Clear();
```

### Event Handler Testing

The `EventHandlerTestHelper` class provides methods to check if specific event handlers are registered/unregistered on a specific event of an object.

#### Example

```csharp
using DroidNet.TestHelpers;

var eventEmitter = new SomeEventEmitter();
var eventHandler = new EventHandler(SomeEventHandler);

eventEmitter.SomeEvent += eventHandler;

// Check if the event handler is registered
var registeredHandlers = EventHandlerTestHelper.FindAllRegisteredDelegates(eventEmitter, "SomeEvent");
registeredHandlers.Should().Contain(eventHandler);

// Check if the event handler is registered for a specific target
var targetHandlers = EventHandlerTestHelper.FindRegisteredDelegates(eventEmitter, "SomeEvent", eventEmitter);
targetHandlers.Should().Contain(eventHandler);

eventEmitter.SomeEvent -= eventHandler;

// Check if the event handler is unregistered
registeredHandlers = EventHandlerTestHelper.FindAllRegisteredDelegates(eventEmitter, "SomeEvent");
registeredHandlers.Should().NotContain(eventHandler);
```
