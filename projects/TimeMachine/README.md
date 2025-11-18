# TimeMachine

A robust and flexible undo/redo system for managing changes in applications. TimeMachine is designed to handle complex scenarios, including nested transactions and change sets, making it suitable for a wide range of applications requiring comprehensive change history management.

## Overview

TimeMachine provides a complete undo/redo framework that enables applications to track, manage, and replay changes. It supports nested transactions, change sets, and integrates seamlessly with dependency injection patterns common in .NET applications.

## Technology Stack

- **Language:** C# 13 (preview)
- **Target Frameworks:** .NET 9.0, .NET 9.0-windows10.0.26100.0
- **Key Dependencies:** .NET Standard library
- **Testing:** MSTest, AwesomeAssertions
- **Code Style:** Following DroidNet C# coding standards with nullable reference types, implicit usings, and strict analysis enabled

## Project Architecture

TimeMachine follows a modular architecture centered around three core concepts:

### Core Components

1. **HistoryKeeper** - Manages the complete change history for a root object
   - Maintains separate undo and redo stacks
   - Handles transaction lifecycle management
   - Supports nested transactions and change sets

2. **UndoRedo** - Singleton instance manager
   - Provides centralized access to HistoryKeeper instances
   - Uses `ConditionalWeakTable<TKey, TValue>` for automatic cleanup
   - Allows independent undo/redo tracking for multiple root objects

3. **Change Abstractions** - Multiple change type implementations
   - `IChange`: Core interface for all changes
   - `Change`: Base class for custom changes
   - `ChangeSet`: Groups multiple changes into atomic units
   - Specialized change types: `SimpleAction`, `ActionWithArgument<T>`, `TargetedChange<T>`, `LambdaExpressionOnTarget<T>`

4. **Transaction System** - Manages grouped operations
   - `ITransaction`: Core transaction interface
   - `Transaction`: Standard transaction implementation
   - `ITransactionFactory`: Creates transaction instances
   - `ITransactionManager`: Orchestrates transaction lifecycle

## Project Structure

```plaintext
projects/TimeMachine/
├── src/
│   ├── TimeMachine.csproj              # Main project file
│   ├── HistoryKeeper.cs                # Change history manager
│   ├── UndoRedo.cs                     # Singleton coordinator
│   ├── HistoryKeeperAddChangeExtensions.cs  # Extension methods
│   ├── StateTransition`1.cs            # State transition handling
│   ├── Changes/                        # Change implementations
│   │   ├── IChange.cs
│   │   ├── Change.cs
│   │   ├── ChangeSet.cs
│   │   ├── SimpleAction.cs
│   │   ├── ActionWithArgument`1.cs
│   │   ├── TargetedChange`1.cs
│   │   └── LambdaExpressionOnTarget`1.cs
│   ├── Transactions/                   # Transaction implementations
│   │   ├── ITransaction.cs
│   │   ├── Transaction.cs
│   │   ├── ITransactionFactory.cs
│   │   └── ITransactionManager.cs
│   └── Properties/
├── tests/
│   ├── TimeMachine.Tests.csproj        # Test project
│   ├── HistoryKeeperTests.*.cs         # Comprehensive test suites
│   ├── HistoryKeeperChangeSetTests.cs
│   ├── HistoryKeeperTransactionTests.cs
│   ├── UndoRedoTests.cs
│   ├── StateTransitionTests.cs
│   ├── Changes/
│   ├── Transactions/
│   └── Properties/
└── README.md
```

## Getting Started

### Installation

Add the TimeMachine package to your project:

```bash
dotnet add package DroidNet.TimeMachine
```

### Basic Usage

#### 1. Create a HistoryKeeper

```csharp
using DroidNet.TimeMachine;

var rootObject = new object();
var historyKeeper = new HistoryKeeper(rootObject);
```

Or use the singleton coordinator:

```csharp
var historyKeeper = UndoRedo.Default[rootObject];
```

#### 2. Add Changes

Add changes directly or within transactions:

```csharp
// Direct change addition
historyKeeper.AddChange("change1", targetObject, t => t.SomeMethod());
historyKeeper.AddChange("change2", arg => Console.WriteLine(arg), "Hello, World!");
historyKeeper.AddChange("change3", () => Console.WriteLine("Action executed"));
```

#### 3. Use Transactions

Group related changes into transactions:

```csharp
using (var transaction = historyKeeper.BeginTransaction("myTransaction"))
{
    historyKeeper.AddChange("change1", obj, o => o.Update());
    historyKeeper.AddChange("change2", () => Console.WriteLine("Done"));
    transaction.Commit();
}
```

#### 4. Undo and Redo

```csharp
// Undo the last transaction
if (historyKeeper.CanUndo)
{
    historyKeeper.Undo();
}

// Redo the undone transaction
if (historyKeeper.CanRedo)
{
    historyKeeper.Redo();
}
```

### Advanced Features

#### Nested Transactions

Support nested transactions for hierarchical change grouping:

```csharp
using (var outerTransaction = historyKeeper.BeginTransaction("outer"))
{
    historyKeeper.AddChange("outer-change1", obj, o => o.Method1());

    using (var innerTransaction = historyKeeper.BeginTransaction("inner"))
    {
        historyKeeper.AddChange("inner-change1", obj, o => o.Method2());
        innerTransaction.Commit();
    }

    outerTransaction.Commit();
}
```

#### Change Sets

Group multiple changes into an atomic unit:

```csharp
historyKeeper.BeginChangeSet("multipleChanges");
historyKeeper.AddChange("change1", obj1, o => o.Update());
historyKeeper.AddChange("change2", obj2, o => o.Refresh());
historyKeeper.EndChangeSet();

// Entire change set undoes/redoes as one unit
historyKeeper.Undo();
```

## Key Classes and Interfaces

### HistoryKeeper

Manages the complete history of changes for a root object.

**Key Properties:**

- `UndoStack`: Collection of undoable changes
- `RedoStack`: Collection of redoable changes
- `CanUndo`: Indicates if changes can be undone
- `CanRedo`: Indicates if changes can be redone
- `Root`: The root object being tracked

**Key Methods:**

- `BeginTransaction(object key)`: Creates a new transaction
- `CommitTransaction(ITransaction transaction)`: Commits a transaction
- `RollbackTransaction(ITransaction transaction)`: Rolls back a transaction
- `AddChange(IChange change)`: Adds a change to history
- `Undo()`: Undoes the last change
- `Redo()`: Redoes the last undone change
- `Clear()`: Clears all undo/redo history
- `BeginChangeSet(object key)`: Starts a new change set
- `EndChangeSet()`: Ends the current change set

### UndoRedo

Provides singleton management of HistoryKeeper instances.

**Properties:**

- `Default`: Gets the singleton instance

**Methods:**

- `this[object root]`: Gets or creates a HistoryKeeper for the specified root
- `Clear()`: Clears all cached histories

### Change Implementations

- **Change**: Base class for custom change implementations
- **ChangeSet**: Container for grouping multiple changes
- **SimpleAction**: Wraps a parameterless action
- **ActionWithArgument**: Wraps an action with a single argument
- **TargetedChange**: Applies changes to a specific target object
- **LambdaExpressionOnTarget**: Uses lambda expressions for changes

## Best Practices

### Change Management

- **Use Transactions**: Always group related changes into transactions for atomicity and consistency
- **Meaningful Identifiers**: Use clear, descriptive keys to identify changes and transactions
- **Nested Transactions**: Properly handle nested transactions; ensure all inner transactions are committed or rolled back before the outer transaction
- **Clean Up History**: Call `Clear()` when appropriate to prevent memory accumulation

### Performance Considerations

- **WeakReference Usage**: HistoryKeeper uses WeakReferences for root objects, allowing garbage collection
- **Memory Management**: The UndoRedo singleton uses `ConditionalWeakTable<TKey, TValue>` for automatic cleanup
- **Large Change Sets**: Monitor the size of undo/redo stacks in long-running applications

### Design Patterns

- **Dependency Injection**: TimeMachine integrates with .NET dependency injection patterns
- **Composition**: Prefer composing changes using the available change types rather than creating new custom types
- **Transaction Factory**: Use `ITransactionFactory` when customizing transaction creation behavior

## Testing

TimeMachine includes comprehensive test coverage using **MSTest** with the **AwesomeAssertions** library.

### Test Structure

Test projects follow the AAA (Arrange-Act-Assert) pattern:

```csharp
[TestClass]
public class HistoryKeeperTests
{
    [TestMethod]
    public void Undo_WhenChangesExist_RemovesLastChange()
    {
        // Arrange
        var keeper = new HistoryKeeper(new object());

        // Act
        keeper.Undo();

        // Assert
        keeper.CanUndo.Should().BeFalse();
    }
}
```

### Running Tests

```bash
# Run all tests
dotnet test projects/TimeMachine/tests/TimeMachine.Tests/TimeMachine.Tests.csproj

# Run with coverage
dotnet test projects/TimeMachine/tests/TimeMachine.Tests/TimeMachine.Tests.csproj `
  /p:CollectCoverage=true /p:CoverletOutputFormat=lcov
```

### Test Categories

- **HistoryKeeperTests.cs**: Core undo/redo functionality
- **HistoryKeeperChangeSetTests.cs**: Change set operations
- **HistoryKeeperTransactionTests.cs**: Transaction management and nesting
- **UndoRedoTests.cs**: Singleton coordinator behavior
- **StateTransitionTests.cs**: State transition handling

## Development Workflow

### Building the Project

```bash
# Build the TimeMachine project
dotnet build projects/TimeMachine/src/TimeMachine/TimeMachine.csproj

# Build the complete solution
cd projects
.\open.cmd
```

### Code Style and Standards

TimeMachine follows the DroidNet C# coding standards:

- **Nullable Reference Types**: Enabled with strict null checking
- **Implicit Usings**: Enabled for standard namespaces
- **Access Modifiers**: Always explicit (`public`, `private`, `protected`, `internal`)
- **Instance Members**: Use `this.` prefix for clarity
- **Documentation**: XML comments on all public APIs
- **Code Analysis**: Configured with StyleCop.Analyzers and Roslynator

See `.github/instructions/csharp_coding_style.instructions.md` for detailed conventions.

### Building and Testing Locally

```bash
# Navigate to TimeMachine project
cd projects/TimeMachine

# Build the project
dotnet build

# Run tests
dotnet test

# Generate and open the solution
.\open.cmd
```

## Contributing

When contributing to TimeMachine:

1. Follow the C# coding standards defined in `.github/instructions/csharp_coding_style.instructions.md`
2. Ensure all tests pass with `dotnet test`
3. Add new tests following the AAA pattern for any new functionality
4. Use meaningful commit messages and reference relevant issues
5. Keep changes focused and well-justified
6. Update documentation if APIs change

### Adding New Change Types

When creating custom changes, inherit from the `Change` base class:

```csharp
public class CustomChange : Change
{
    public required object Target { get; init; }

    public override void Apply()
    {
        // Implement change logic
    }
}
```

### Adding New Transaction Types

Implement `ITransaction` for custom transaction behavior:

```csharp
public class CustomTransaction : ITransaction
{
    public void AddChange(IChange change)
    {
        // Add change to transaction
    }

    public void Commit()
    {
        // Commit transaction
    }

    public void Rollback()
    {
        // Rollback transaction
    }
}
```

## License

TimeMachine is distributed under the **MIT License**. See the LICENSE file in the repository root for details.

## Resources

- **Main Project Structure:** `projects/README.md`
- **C# Coding Standards:** `.github/instructions/csharp_coding_style.instructions.md`
- **Testing Guidelines:** `.github/prompts/csharp-mstest.prompt.md`
- **Build System:** `Directory.Packages.props`, `Directory.build.props`
