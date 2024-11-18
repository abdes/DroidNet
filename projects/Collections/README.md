# Extended Collections

Welcome to **DroidNet.Collections**, a lightweight .NET class library providing extension methods and custom collections to enhance your application's functionality.

## Overview

This library aims to simplify common tasks when working with [ObservableCollection](https://learn.microsoft.com/en-us/dotnet/api/system.collections.objectmodel.observablecollection-1?view=net-8.0) in .NET, such as:

- Inserting an item into a sorted `ObservableCollection<T>`.
- Transforming elements of one collection to another using a given function.
- Creating an observable collection that mirrors its source and applies a specific transformation to its elements on the fly.

## Classes & Methods

### 1. DynamicObservableCollection<TSource, TResult>

This disposable collection mirrors its source `ObservableCollection<TSource>` and applies a given transformation function to each element.

**Example usage:**

```csharp
var source = new ObservableCollection<int> { 1, 2, 3 };
var result = new DynamicObservableCollection<int, string>(source, x => x.ToString());

// Now, any changes to 'source' will be reflected in 'result'
source.Add(4);
Console.WriteLine(string.Join(", ", result)); // Output: "1, 2, 3, 4"

// Don't forget to dispose when no longer needed
result.Dispose();
```

### 2. ObservableCollectionExtensions

The `ObservableCollectionExtensions` class offers two extension methods:

#### a. InsertInPlace<TItem, TOrderBy>(ObservableCollection<TItem>, TItem, Func<TItem, TOrderBy>, Comparer<TOrderBy>)

This method inserts an item into a sorted `ObservableCollection<T>` at the correct index based on a given key getter and comparer.

**Example usage:**

```csharp
var collection = new ObservableCollection<Person>();
collection.InsertInPlace(new Person { Name = "John Doe", Age = 30 }, p => p.Age, Comparer<int>.Default);
```

#### b. Transform<TSource, TResult>(ObservableCollection<TSource>, Func<TSource, TResult>)

This method creates a new `DynamicObservableCollection<TSource, TResult>` containing the elements of the source collection after applying a given transformation function.

**Example usage:**

```csharp
var source = new ObservableCollection<int> { 1, 2, 3 };
var result = source.Transform(x => x.ToString());
source.Add(4); // result is now {"1", "2", "3", "4"}
result.Dispose();
```

## Getting Started

To get started with using **Collection**, install the package via NuGet:

```
dotnet add package DroidNet.Collections
```

Happy coding! 🚀
