# TimeMachine

TimeMachine is a robust, composable undo/redo module for .NET applications. It is designed for complex scenarios including nested transactions, change sets, and (new) asynchronous change application.

## Highlights

- **Undo/Redo stacks per root** via `UndoRedo.Default[root]` (weak-keyed and GC-friendly)
- **Transactions** to group multiple changes into a single undo/redo entry
- **Change sets** to group changes produced during undo/redo of grouped operations
- **Async support**: changes can be applied synchronously or asynchronously

## Technology

- **Language:** C# 13 (preview)
- **Target frameworks:** .NET 9.0, .NET 9.0-windows10.0.26100.0
- **Testing:** MSTest + AwesomeAssertions (+ Moq)

## Concepts

### HistoryKeeper

`HistoryKeeper` manages the undo/redo history for one “root” object.

Key capabilities:

- Tracks `UndoStack` and `RedoStack`
- Clears redo history when new (non-undo/redo) changes are added
- Supports nested `ITransaction` blocks
- Supports change sets for atomic undo/redo of grouped operations

### Changes

Changes implement `IChange` and are the unit of undo/redo.

- `IChange.Apply()` is the synchronous execution path.
- `IChange.ApplyAsync(CancellationToken)` is the asynchronous execution path.

Async model:

- By default, `ApplyAsync(...)` calls `Apply()`.
- Async-only changes throw from `Apply()` to avoid accidental blocking/deadlocks.

### Transactions

Transactions (`ITransaction`) are also changes, allowing a transaction to be pushed as a single entry to the undo/redo stacks.

## Project Structure

```plaintext
projects/TimeMachine/
├── src/
│   ├── TimeMachine.csproj
│   ├── HistoryKeeper.cs
│   ├── UndoRedo.cs
│   ├── HistoryKeeperAddChangeExtensions.cs
│   ├── StateTransition`1.cs
│   ├── Changes/
│   │   ├── IChange.cs
│   │   ├── Change.cs
│   │   ├── ChangeSet.cs
│   │   ├── SimpleAction.cs
│   │   ├── ActionWithArgument`1.cs
│   │   ├── TargetedChange`1.cs
│   │   ├── LambdaExpressionOnTarget`1.cs
│   │   ├── AsyncSimpleAction.cs
│   │   ├── AsyncActionWithArgument`1.cs
│   │   └── AsyncLambdaExpressionOnTarget`1.cs
│   ├── Transactions/
│   │   ├── ITransaction.cs
│   │   ├── Transaction.cs
│   │   ├── ITransactionFactory.cs
│   │   └── ITransactionManager.cs
│   └── Properties/
├── tests/
│   ├── TimeMachine.Tests.csproj
│   ├── TimeMachine.Async.Tests/
│   │   ├── TimeMachine.Async.Tests.csproj
│   │   └── (async-only test suites)
│   └── (sync test suites)
└── README.md
```

## Getting Started

### Installation

```bash
dotnet add package DroidNet.TimeMachine
```

### Create a HistoryKeeper

```csharp
using DroidNet.TimeMachine;

var root = new object();
var historyKeeper = UndoRedo.Default[root];
```

### Mental model (undo/redo in one minute)

TimeMachine can feel “backwards” at first, especially if you are new to undo/redo.

The key idea:

- When the user performs an operation, you **do the operation now**.
- Immediately after that, you **queue the reverse operation** (the “undo”) using `AddChange(...)`.
- When an undo runs, it can (and usually should) **queue the reverse of the undo** (the “redo”).

So you are not telling TimeMachine “what I just did”. You are telling it “how to undo what I just did”.

Mermaid flow (what happens and when):

```mermaid
flowchart TD
    U[User action happens now<br/>e.g. SetTitle Hello] --> Q[Queue UNDO change<br/>AddChange: restore old value]
    Q --> S[UndoStack has one entry]

    S -->|"Undo() / UndoAsync()"| A[Apply UNDO change]
    A --> RQ[UNDO queues REDO change<br/>AddChange inside undo]
    RQ --> RS[RedoStack has one entry]

    RS -->|"Redo() / RedoAsync()"| RA[Apply REDO change]
    RA --> UQ[REDO queues UNDO change<br/>AddChange inside redo]
    UQ --> S
```

Example (sync):

```csharp
var value = 0;

void Increment()
{
    value++;

    // Queue the reverse operation (undo).
    historyKeeper.AddChange("decrement", Decrement);
}

void Decrement()
{
    value--;

    // When undoing (i.e., applying the undo change), register the redo operation.
    historyKeeper.AddChange("increment", Increment);
}

Increment();

historyKeeper.Undo(); // value goes back
historyKeeper.Redo(); // value goes forward again
```

## Rules of Thumb (copy/paste into your brain)

TimeMachine is simple when you follow one invariant:

- Every operation has a single inverse.
- When you perform an operation **now**, you register its inverse via `AddChange(...)`.
- When an inverse runs (during **Undo**/**Redo**), it should register the inverse of itself.

That’s it.

### The three rules

1. **Do the thing now.**
2. **Queue only the inverse.** (Don’t queue both undo and redo “up front”.)
3. **Never add two history entries for one user action.**

### What not to do (common AI/codegen failure modes)

- **Anti-pattern: queue undo and redo immediately**
  - “I just added the component, so I’ll `AddChange(remove)` and also `AddChange(add)`.”
  - Result: duplicate entries / broken stacks.

- **Anti-pattern: make Undo call a method that also queues history**
  - If your “business method” always calls `AddChange`, then calling it from inside an undo change will usually create extra history entries.
  - Fix: separate “apply mutation” from “register inverse”, or use a dedicated `ApplyXxx(...)` method for undo/redo.

- **Anti-pattern: use volatile UI indices for undo**
  - UI indices often depend on expansion/virtualization/filtering.
  - Fix: store stable model coordinates (parent + child-relative index, ids, snapshots, etc.).

## Patterns that work well

### Pattern 1: Two explicit operations (no ambiguity)

Use two explicit functions: `DoX()` and `UndoX()`.
Each one performs its work and queues only its inverse.

You already saw this in the `Increment`/`Decrement` example above.

### Pattern 2: Use `AddChange(Action<TArg>, arg)` to avoid nested lambdas

If you want clean code without “double lambdas” or self-referencing delegates, use the overload that takes an action + argument.

This is especially good when the inverse needs *captured data* (like a node reference + the component instance).

```csharp
public sealed record ComponentOp(SceneNode Node, GameComponent Component);

void ApplyAdd(ComponentOp op)
{
        _ = op.Node.AddComponent(op.Component);
        historyKeeper.AddChange($"remove ({op.Component.Name})", ApplyRemove, op);
}

void ApplyRemove(ComponentOp op)
{
        _ = op.Node.RemoveComponent(op.Component);
        historyKeeper.AddChange($"add ({op.Component.Name})", ApplyAdd, op);
}

// User action:
var op = new ComponentOp(node, component);
ApplyAdd(op); // does it now + queues inverse
```

Key properties of this pattern:

- One user action → one `AddChange(...)` entry (the inverse)
- Undo/redo correctness doesn’t depend on “current selection” or UI state
- The operation argument (`op`) contains exactly what’s needed to replay the inverse

### Pattern 3: Snapshot + apply/restore for property edits

For property edits, store “before” and “after” snapshots and implement explicit apply/restore methods.

```csharp
public sealed record TransformOp(SceneNode Node, TransformSnapshot Old, TransformSnapshot New);

void RestoreTransform(TransformOp op)
{
        ApplySnapshot(op.Node, op.Old);
        historyKeeper.AddChange("reapply transform", ReapplyTransform, op);
}

void ReapplyTransform(TransformOp op)
{
        ApplySnapshot(op.Node, op.New);
        historyKeeper.AddChange("restore transform", RestoreTransform, op);
}

// User action already applied the New snapshot:
historyKeeper.AddChange("restore transform", RestoreTransform, new TransformOp(node, oldSnap, newSnap));
```

## Guidance for AI Agents / Codegen

If you are generating code that uses TimeMachine:

- Prefer explicit `ApplyXxx(op)` methods + `AddChange(Action<TArg>, arg)`.
- Never queue both undo and redo for the same user action.
- Never assume the “current selection” at undo time.
- Store stable operands (references, ids, snapshots, parent+index), not UI indices.

### Add synchronous changes

```csharp
var oldTitle = document.Title;
document.SetTitle("Hello");

// Queue the undo (set title back to the previous value).
historyKeeper.AddChange("set-title", document, d => d.SetTitle(oldTitle));

// You can also queue simple actions.
historyKeeper.AddChange("log", () => Console.WriteLine("Undo: restore previous state"));

// And actions that take arguments.
historyKeeper.AddChange("append", (string? s) => Console.WriteLine(s), "Undo message");
```

### Undo/Redo (synchronous)

```csharp
if (historyKeeper.CanUndo)
{
    historyKeeper.Undo();
}

if (historyKeeper.CanRedo)
{
    historyKeeper.Redo();
}
```

## Async Support

TimeMachine supports asynchronous changes end-to-end:

- `IChange.ApplyAsync(...)` exists on the core change contract.
- `HistoryKeeper.UndoAsync(...)` and `HistoryKeeper.RedoAsync(...)` are the async execution entry points.

### Add asynchronous changes (Task-based delegates)

The extension methods accept Task-returning delegates so `async` lambdas are unambiguous.

```csharp
using System.Threading.Tasks;

historyKeeper.AddChange("undo", async () =>
{
    await Task.Delay(50).ConfigureAwait(false);

    // When undoing, register the redo action.
    historyKeeper.AddChange("redo", async () =>
    {
        await Task.Delay(50).ConfigureAwait(false);
    });
});
```

### Undo/Redo (asynchronous)

```csharp
if (historyKeeper.CanUndo)
{
    await historyKeeper.UndoAsync().ConfigureAwait(false);
}

if (historyKeeper.CanRedo)
{
    await historyKeeper.RedoAsync().ConfigureAwait(false);
}
```

### Mixing synchronous and asynchronous changes

You can mix synchronous and asynchronous changes in the same history (including inside transactions and change sets).

Guidelines:

- **Mixing is allowed:** you can register sync and async changes together.
- **Async execution is superset:** synchronous changes are compatible with the async pipeline because the default `ApplyAsync(...)` delegates to `Apply()`.
- **Do not use `Undo()` / `Redo()` if async-only changes may exist:** async-only changes throw from `Apply()` to prevent accidental sync execution.

Practical rule of thumb:

- If a history may contain any async-only change, treat the whole history as async and always use `UndoAsync()` / `RedoAsync()`.

Example (mixed history, executed asynchronously):

```csharp
historyKeeper.AddChange("sync-log", () => Console.WriteLine("sync undo"));

historyKeeper.AddChange("async-undo", async () =>
{
    await Task.Delay(10).ConfigureAwait(false);
});

await historyKeeper.UndoAsync().ConfigureAwait(false);
```

## Transactions

Use transactions to group multiple changes into a single undo/redo entry.

```csharp
using (var transaction = historyKeeper.BeginTransaction("edit-document"))
{
    historyKeeper.AddChange("set-title", document, d => d.SetTitle("New"));
    historyKeeper.AddChange("set-author", document, d => d.SetAuthor("You"));
    transaction.Commit();
}
```

Nested transactions are supported.

## Change Sets

Change sets group changes into an atomic unit until `EndChangeSet()`.

```csharp
historyKeeper.BeginChangeSet("batch");
historyKeeper.AddChange("op1", obj1, o => o.Update());
historyKeeper.AddChange("op2", obj2, o => o.Refresh());
historyKeeper.EndChangeSet();
```

## Extending TimeMachine

### Custom change (sync)

```csharp
using DroidNet.TimeMachine.Changes;

public sealed class CustomChange : Change
{
    public required Action ApplyAction { get; init; }

    public override void Apply() => this.ApplyAction();
}
```

### Custom change (async)

```csharp
using System.Threading;
using System.Threading.Tasks;
using DroidNet.TimeMachine.Changes;

public sealed class CustomAsyncChange : Change
{
    public required Func<CancellationToken, ValueTask> ApplyActionAsync { get; init; }

    public override void Apply() => throw new InvalidOperationException("Use ApplyAsync for this change.");

    public override ValueTask ApplyAsync(CancellationToken cancellationToken = default) => this.ApplyActionAsync(cancellationToken);
}
```

## Testing

Run tests:

```bash
dotnet test -c Release --project projects/TimeMachine/tests/TimeMachine.Tests.csproj
```

## Build

```bash
dotnet build -c Release projects/TimeMachine/src/TimeMachine.csproj
```

## License

TimeMachine is distributed under the MIT License. See the repository root LICENSE file.
