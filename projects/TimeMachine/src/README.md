# TimeMachine

The TimeMachine project provides a robust and flexible undo/redo system for managing changes in applications. It is designed to handle complex scenarios, including nested transactions and change sets, making it suitable for a wide range of applications.

## Features

- **Undo/Redo Functionality**: Easily undo and redo changes.
- **Nested Transactions**: Support for nested transactions to group related changes.
- **Change Sets**: Group multiple changes into a single change set.
- **Dependency Injection**: Uses dependency injection for managing transactions and changes.

## Getting Started

To get started with the TimeMachine project, follow these steps:

1. **Install the Package**: Add the TimeMachine package to your project.
2. **Initialize HistoryKeeper**: Create an instance of `HistoryKeeper` for the root object you want to track.
3. **Begin Transactions**: Use transactions to group related changes.
4. **Add Changes**: Add changes to the transaction or directly to the `HistoryKeeper`.
5. **Commit or Rollback**: Commit or rollback transactions as needed.
6. **Undo/Redo**: Use the `Undo` and `Redo` methods to manage changes.

## Example Usage

___csharp
using DroidNet.TimeMachine;
using DroidNet.TimeMachine.Changes;
using DroidNet.TimeMachine.Transactions;

public class Example
{
    public void Main()
    {
        var rootObject = new object();
        var historyKeeper = new HistoryKeeper(rootObject);

        using (var transaction = historyKeeper.BeginTransaction("exampleTransaction"))
        {
            var change = new CustomChange { Key = "change1" };
            transaction.AddChange(change);
            transaction.Commit();
        }

        historyKeeper.Undo();
        historyKeeper.Redo();
    }
}

public class CustomChange : Change
{
    public override void Apply()
    {
        // Implement the change logic here
    }
}
+++

## Classes and Interfaces

### HistoryKeeper

The `HistoryKeeper` class manages the history of changes for undo/redo operations.

**Properties**:
- `UndoStack`: A collection of undoable changes.
- `RedoStack`: A collection of redoable changes.
- `CanUndo`: Indicates if there are changes to undo.
- `CanRedo`: Indicates if there are changes to redo.
- `Root`: The root object for which changes are tracked.

**Methods**:
- `BeginTransaction(object key)`: Begins a new transaction.
- `CommitTransaction(ITransaction transaction)`: Commits the provided transaction.
- `RollbackTransaction(ITransaction transaction)`: Rolls back the provided transaction.
- `AddChange(IChange change)`: Adds a change to the appropriate stack.
- `Undo()`: Undoes the first available change.
- `Redo()`: Redoes the first available change.
- `Clear()`: Clears the undo and redo stacks.
- `BeginChangeSet(object key)`: Begins a new change set.
- `EndChangeSet()`: Ends the current change set.

**Example Usage**:
___csharp
var historyKeeper = new HistoryKeeper(rootObject);
historyKeeper.BeginChangeSet("changeSet1");
// Add changes
historyKeeper.EndChangeSet();
historyKeeper.Undo();
historyKeeper.Redo();
+++

### Change

The `Change` class represents an individual change that can be applied.

**Properties**:
- `Key`: An object to identify the change.

**Methods**:
- `Apply()`: Applies the change.
- `ToString()`: Returns the string representation of the `Key`.

### ChangeSet

The `ChangeSet` class represents a collection of changes.

**Properties**:
- `Changes`: An enumerable to access the changes.

**Methods**:
- `Apply()`: Applies all changes in the set.
- `Add(IChange change)`: Adds a change to the set.

### UndoRedo

The `UndoRedo` class provides a singleton instance to manage undo/redo operations.

**Properties**:
- `Default`: Returns the singleton instance.

**Methods**:
- `this[object root]`: Gets or creates a `HistoryKeeper` for the specified root object.
- `Clear()`: Clears the cached undo roots.

### Transaction

The `Transaction` class represents a transaction that groups multiple changes.

**Properties**:
- `Changes`: An enumerable to access the changes.
- `Key`: The key associated with the transaction.

**Methods**:
- `Dispose()`: Disposes of the transaction.
- `Commit()`: Commits the transaction.
- `Rollback()`: Rolls back the transaction.
- `AddChange(IChange change)`: Adds a change to the transaction.
- `Apply()`: Applies all changes in the transaction.
- `ToString()`: Returns the string representation of the `Key`.

## Best Practices

- **Use Transactions**: Group related changes into transactions to ensure atomicity.
- **Handle Nested Transactions**: Be mindful of nested transactions and ensure they are properly committed or rolled back.
- **Clear History**: Use the `Clear` method to reset the undo/redo stacks when necessary.
- **Identify Changes**: Use meaningful keys to identify changes and change sets.
