# DroidNet Collections

A lightweight .NET class library providing extension methods and custom collections to fill gaps in .NET's standard collection utilities. This library offers specialized collections for reactive applications (including [`ObservableCollection<T>`](https://learn.microsoft.com/en-us/dotnet/api/system.collections.objectmodel.observablecollection-1?view=net-10.0)-based transformations and filtering), advanced data structures (such as order-statistic trees for efficient rank/select operations), and convenience extension methods for common collection patterns.

## Overview

**DroidNet.Collections** addresses several common patterns in .NET applications:

- **Dynamic transformations:** Create an observable collection that mirrors a source collection while automatically applying transformations to elements.
- **Sorted insertion:** Insert items into a sorted `ObservableCollection<T>` at the correct index using custom key extraction and comparison logic.
- **Filtered views:** Maintain a real-time, read-only filtered view over an observable collection that responds to both collection changes and individual property changes.
- **Delta-based hierarchical filtering:** Support strict, builder-driven filtered views that can maintain structural closure (e.g., ancestors/lineage) while still emitting minimal, incremental deltas (no Reset spam).
- **Order-statistic operations:** A balanced red-black tree supporting efficient rank and select operations in O(log n) time, enabling fast positional queries and element retrieval by index in sorted data.

## Technology Stack

- **Platform:** .NET 9.0 (cross-platform) and .NET 9.0-windows10.0.26100.0 (Windows)
- **Language:** C# 13 preview with nullable reference types enabled
- **Core Dependencies:** None (uses only .NET standard libraries)
- **Testing:** MSTest 4.0 with AwesomeAssertions for fluent assertions
- **Code Quality:** Strict Roslyn analyzer configuration, StyleCop.Analyzers, Roslynator

## Project Architecture

This module is part of the modular **DroidNet** mono-repository (see [DroidNet README](../../README.md) for architecture overview). Collections is a self-contained utility library with:

- **src/:** Source implementation of extension methods and custom collection classes
- **tests/:** Comprehensive MSTest suite with AAA pattern test organization
- Clean public API with careful composition design
- MIT License for maximum compatibility

### Key Components

1. **DynamicObservableCollection<TSource, TResult>:** A disposable collection that mirrors a source `ObservableCollection<TSource>` and applies a transformation function to each element. Automatically synchronizes with source collection changes.

2. **FilteredObservableCollection&lt;T&gt;:** A read-only, filtered view over an `ObservableCollection<T>` that supports both collection-level and property-level change tracking and preserves source ordering.

    **Robust delta change handling (strict mode):**
    - Supports a **builder-driven** mode where a small “delta builder” provides the *additional* items to include/exclude when an item’s predicate result changes (for example, include ancestors when a descendant matches).
    - Enforces **strict reference identity** for builder outputs: builders must return the **exact source instances**. Returning equal-but-distinct objects is treated as a contract violation.
    - Applies changes as **minimal deltas** (adds/removes in contiguous runs) and avoids `Reset` spam during normal operation.
    - Keeps predicate refresh incremental via `ReevaluatePredicate()` (re-checks all items and applies the resulting delta without rebuilding the source).
    - Designed to keep UI controls stable by applying mutations *before* raising `CollectionChanged` events so indices and `Count` match the event stream.

    See the detailed design notes in:
    - `projects/Collections/DESIGN/FilteredObservableCollection.DeltaBuilder.md`

    Options and defaults:
    - The type is `sealed` to avoid derived-class complexity and make behavior explicit.
    - The implementation uses an explicit, documented `NotificationSuspender` struct for deferral and provides richer XML documentation for public and explicit interface members.

    | Option | Purpose | Default |
    |---|---|---:|
    | `ObservableCollection<string> ObservedProperties` | Empty means do not observe item property changes; otherwise only those properties trigger predicate re-checks for the changed item. | `[]` (observe none) |
    | `TimeSpan PropertyChangedDebounceInterval` | Debounce window for coalescing property-change driven updates into a single batch. | `TimeSpan.Zero` |
    | `IDisposable DeferNotifications()` | Temporarily suspends raising `CollectionChanged`; when disposed, rebuilds and raises a `Reset` if changes occurred while suspended. | N/A |

3. **OrderStatisticTreeCollection&lt;T&gt;:** A balanced order-statistic binary search tree (red-black) that maintains elements in sorted order while supporting rank and select operations in O(log n) time. Implements `IReadOnlyCollection<T>` for enumeration and supports:
   - `Add(T item)` – Insert elements; duplicates are allowed
   - `Remove(T item)` – Remove element equal to the given item
   - `Rank(T item)` – Count of elements strictly less than a given value
   - `Select(int index)` – Retrieve the element at a given in-order index
   - `Contains(T item)` – Check for element existence
   - Enumeration in in-order (sorted) sequence
   - Customizable comparison via `IComparer<T>`

4. **ObservableCollectionExtensions:** Extension methods for `ObservableCollection<T>` including:
   - `InsertInPlace<TItem, TOrderBy>()` – Insert items into sorted collections using key extraction and comparison
   - `Transform<TSource, TResult>()` – Create a dynamic transformed view using a helper method

## Getting Started

### Installation

Install via NuGet Package Manager:

```powershell
dotnet add package DroidNet.Collections
```

Or via the .NET CLI:

```powershell
dotnet package add DroidNet.Collections
```

### Quick Examples

#### Example 1: Transform a Collection

```csharp
using System.Collections.ObjectModel;
using DroidNet.Collections;

var source = new ObservableCollection<int> { 1, 2, 3 };
var transformed = new DynamicObservableCollection<int, string>(
    source,
    x => $"Item {x * 10}"
);

// Automatically reflects source changes
source.Add(4);
Console.WriteLine(string.Join(", ", transformed));
// Output: "Item 10, Item 20, Item 30, Item 40"

// Remember to dispose when done
transformed.Dispose();
```

#### Example 2: Insert into Sorted Collection

```csharp
using System.Collections.ObjectModel;
using DroidNet.Collections;

var collection = new ObservableCollection<int> { 1, 3, 5, 7 };
collection.InsertInPlace(4, x => x, Comparer<int>.Default);
Console.WriteLine(string.Join(", ", collection));
// Output: "1, 3, 4, 5, 7"
```

#### Example 3: Use Order-Statistic Tree for Rank/Select Operations

```csharp
using DroidNet.Collections;

var tree = new OrderStatisticTreeCollection<int>();
for (int i = 0; i < 10; i++)
{
    tree.Add(i * 2);  // Add: 0, 2, 4, 6, 8, 10, 12, 14, 16, 18
}

// Rank: count elements strictly less than the given value
int rank = tree.Rank(5);  // Returns 3 (elements 0, 2, 4 are less than 5)

// Select: retrieve element at a given in-order index
int element = tree.Select(5);  // Returns 10 (6th element in sorted order)

// Enumeration in sorted order
foreach (var value in tree)
{
    Console.WriteLine(value);
}

// Contains and Remove
bool found = tree.Contains(6);  // Returns true
tree.Remove(6);
```

#### Example 4: Create a Filtered View

```csharp
using System.Collections.ObjectModel;
using System.ComponentModel;
using DroidNet.Collections;

public class Item : INotifyPropertyChanged
{
    private int _count;
    public int Count
    {
        get => _count;
        set
        {
            if (_count != value)
            {
                _count = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Count)));
            }
        }
    }

    public event PropertyChangedEventHandler? PropertyChanged;
}

var source = new ObservableCollection<Item>
{
    new() { Count = 5 },
    new() { Count = 10 },
    new() { Count = 3 },
};
// Create a filtered view that only observes the Count property changes
var opts = new FilteredObservableCollectionOptions();
opts.ObservedProperties.Add(nameof(Item.Count));
var filtered = FilteredObservableCollectionFactory.FromPredicate(
    source,
    item => item.Count > 4,
    opts);

// View shows items with Count > 4
Console.WriteLine(filtered.Count); // Output: 2

// You can opt out of item-level observation by leaving ObservedProperties empty (default)
var opts2 = new FilteredObservableCollectionOptions();
using var collOnly = FilteredObservableCollectionFactory.FromPredicate(source, item => item.Count > 4, opts2);

// Batch multiple updates and only raise a single Reset when done
using (filtered.DeferNotifications())
{
    source[0].Count = 1;  // will not immediately raise events
    source.Add(new Item { Count = 20 });
}
// After the using block, a single Reset is raised if changes occurred

// If you change the predicate inputs externally, you can trigger an incremental re-check
// without rebuilding the view:
filtered.ReevaluatePredicate();

// Dispose when done
filtered.Dispose();
collOnly.Dispose();
```

## Development Workflow

### Building the Project

Build the entire project:

```powershell
dotnet build projects/Collections/Collections.sln
```

Or build just the source:

```powershell
dotnet build projects/Collections/src/Collections/Collections.csproj
```

### Running Tests

Run all tests in the project:

```powershell
dotnet test --project projects/Collections/tests/Collections.Tests/Collections.Tests.csproj
```

Run tests with coverage:

```powershell
dotnet test --project projects/Collections/tests/Collections.Tests/Collections.Tests.csproj `
  /p:CollectCoverage=true /p:CoverletOutputFormat=lcov
```

### Code Style & Standards

This project follows the DroidNet repository coding standards:

- **C# version:** C# 13 preview features with nullable reference types enabled (`<Nullable>enable</Nullable>`)
- **Code style:** StyleCop.Analyzers with strict Roslyn diagnostics
- **Access modifiers:** Always explicit (`public`, `private`, `internal`, etc.)
- **Instance members:** Prefixed with `this.` when accessed
- **API design:** Prefer composition, small API surfaces, and well-documented public interfaces

See [csharp_coding_style.instructions.md](../../.github/instructions/csharp_coding_style.instructions.md) for detailed coding standards.

## Testing Approach

This project uses **MSTest** with the AAA (Arrange-Act-Assert) pattern:

- **Test organization:** Test classes use `[TestClass]` and `[TestMethod]` attributes
- **Test naming:** `MethodName_Scenario_ExpectedBehavior`
- **Assertions:** AwesomeAssertions for fluent, readable assertions
- **Categories:** Tests are organized by category (e.g., `[TestCategory("Dynamic Observable Collection")]`)
- **Exclusions:** Test classes marked with `[ExcludeFromCodeCoverage]` for accurate coverage metrics

Example test structure:

```csharp
[TestClass]
public class DynamicObservableCollectionTests
{
    [TestMethod]
    public void Initialize_WhenSourceHasItems_TransformsItems()
    {
        // Arrange
        var source = new ObservableCollection<int> { 1, 2, 3 };

        // Act
        var result = new DynamicObservableCollection<int, string>(source, x => x.ToString());

        // Assert
        result.Should().HaveCount(3);
        result.Should().Contain("1", "2", "3");
    }
}
```

See [MSTest conventions](../../.github/prompts/csharp-mstest.prompt.md) for more details.

## Contributing

Contributions are welcome! When contributing to this project:

1. **Follow the code style:** Refer to [csharp_coding_style.instructions.md](../../.github/instructions/csharp_coding_style.instructions.md) for conventions
2. **Write tests:** New features must include MSTest tests following the AAA pattern
3. **Document public APIs:** Use XML documentation comments on public members
4. **Keep it focused:** Make small, focused changes with clear intent
5. **Reference existing patterns:** Check existing source code before proposing architectural changes

For more details on the repository structure and contributing guidelines, see the [DroidNet README](../../README.md) and [Copilot Instructions](../../.github/copilot-instructions.md).

## License

This project is licensed under the MIT License. See [LICENSE](../../LICENSE) for details.
