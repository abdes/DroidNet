# OxCo Batch Processing Examples

This directory contains **three main examples** that demonstrate different approaches to batch collection processing using OxCo coroutines. Each example is organized in its own subdirectory with dedicated build targets.

## 🎯 Main Examples (Use These)

| Example              | Directory           | File                            | Description                                                                       |
| -------------------- | ------------------- | ------------------------------- | --------------------------------------------------------------------------------- |
| **YieldAwaiter**     | `YieldAwaiter/`     | `yield_awaiter_example.cpp`     | Sequential batch processing with custom YieldAwaiter and caller result population |
| **BroadcastChannel** | `BroadcastChannel/` | `broadcast_channel_example.cpp` | Parallel batch processing with OxCo BroadcastChannel and caller result population |
| **RepeatableShared** | `RepeatableShared/` | `repeatable_shared_example.cpp` | Per-item sequential processing with RepeatableShared synchronization              |

**These three examples provide complete, production-ready patterns** with different synchronization and processing approaches. Choose one based on your specific requirements.

## 📁 Directory Structure

```
BatchExecution/
├── Common/                          # Shared utilities and examples
│   ├── BatchExecutionEventLoop.h    # Base event loop implementation
│   ├── shared_examples.h            # Common test cases and examples
│   └── shared_utilities.h           # Data structures and predicates
├── Common.h                         # Main include for all shared functionality
├── YieldAwaiter/                    # Custom awaiter approach
│   ├── CMakeLists.txt
│   └── yield_awaiter_example.cpp
├── BroadcastChannel/                # Broadcast channel approach
│   ├── CMakeLists.txt
│   └── broadcast_channel_example.cpp
├── RepeatableShared/                # RepeatableShared approach
│   ├── CMakeLists.txt
│   └── repeatable_shared_example.cpp
└── CMakeLists.txt                   # Main build configuration
```

## 🚀 Quick Start

```powershell
# Build all three examples (targets are auto-generated from module names)
cmake --build out\build --target Oxygen.OxCo.Examples.BatchExecution.YieldAwaiter
cmake --build out\build --target Oxygen.OxCo.Examples.BatchExecution.BroadcastChannel
cmake --build out\build --target Oxygen.OxCo.Examples.BatchExecution.RepeatableShared

# Run them to see the different approaches
.\out\build\bin\Debug\Oxygen.OxCo.Examples.BatchExecution.YieldAwaiter.exe
.\out\build\bin\Debug\Oxygen.OxCo.Examples.BatchExecution.BroadcastChannel.exe
.\out\build\bin\Debug\Oxygen.OxCo.Examples.BatchExecution.RepeatableShared.exe
```

## 🔗 Shared Components

All examples use common components from the `Common/` directory:

- **`shared_utilities.h`**: Common data structures (`ElementData`), test collections, and predicates (`is_even`, `is_odd`, `greater_than_10`, etc.)
- **`shared_examples.h`**: Three standardized test cases used across all approaches for consistent comparison
- **`BatchExecutionEventLoop.h`**: Base event loop implementation shared by all examples
- **`Common.h`**: Main header that includes all shared functionality

## 🏗️ Architecture Comparison

### YieldAwaiter Approach

```cpp
// Custom awaiter for fine-grained control
struct YieldAwaiter {
  YieldAwaiterLoop* loop;
  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<> handle) {
    loop->Schedule([handle]() { handle.resume(); });
  }
  void await_resume() { }
};

// Sequential processing with explicit yielding
for (size_t i = 0; i < collection_.size(); ++i) {
  // Process element...
  co_await YieldAwaiter { &loop_ };
}
```

**Characteristics:**

- ✅ Simple, predictable execution
- ✅ Lower memory overhead
- ✅ Fine control over scheduling
- ❌ Sequential processing only
- ❌ Custom awaiter complexity

### BroadcastChannel Approach

```cpp
// Each operation gets its own channel reader
auto reader = element_channel.ForRead();
while (auto element = co_await reader.Receive()) {
  // Process element concurrently...
}

// Traversal broadcasts to all operations
co_await writer.Send(std::move(element));
co_await Yield{}; // Built-in OxCo yielding
```

**Characteristics:**

- ✅ True parallel processing
- ✅ Idiomatic OxCo usage
- ✅ Natural early termination
- ✅ Scalable architecture
- ❌ Higher overhead
- ❌ More complex setup

### RepeatableShared Approach

```cpp
// Create RepeatableShared with element producer
RepeatableShared<ElementData> element_source {
  [this, &current_index]() -> Co<ElementData> {
    // Provide next element
    co_return ElementData(collection_[current_index++], current_index - 1);
  }
};

// Each operation processes items sequentially
auto element = co_await source.Next();
auto lock = co_await source.Lock();
// Process element...
```

**Characteristics:**

- ✅ Sequential per-item processing
- ✅ All operations complete on each item before next
- ✅ Simpler than BroadcastChannel
- ✅ Built-in synchronization
- ❌ Not truly parallel
- ❌ More complex than YieldAwaiter

## 🎛️ Which Example Should I Use?

### Choose YieldAwaiter When:

- ✅ Performance is critical and overhead must be minimal
- ✅ Simple, predictable batch operations
- ✅ Sequential processing is acceptable
- ✅ You need fine control over coroutine scheduling

### Choose BroadcastChannel When:

- ✅ Operations benefit from parallel execution
- ✅ Complex operations with different completion logic
- ✅ You want idiomatic OxCo code using built-in primitives
- ✅ Scalability and extensibility are important

### Choose RepeatableShared When:

- ✅ You need per-item processing (all operations on item N before item N+1)
- ✅ You want simpler setup than BroadcastChannel
- ✅ Operations need to be synchronized per-item
- ✅ Sequential item ordering is important

## Key Features Demonstrated

All three main examples show:

- **Identical ExecuteBatch() API**: Same lambda-based batch operation registration using shared components
- **Same Three Test Cases**: Multiple result types, prime analysis, and range analysis from `shared_examples.h`
- **Caller Result Population**: Safe patterns for populating `std::vector`, `std::optional`, `size_t&`, etc.
- **Same Result Containers**: Identical input/output for easy comparison using `shared_utilities.h`
- **Memory Safety**: No dangling references or use-after-free issues
- **Modular Architecture**: Clean separation between synchronization approaches and common functionality

**The only difference is the underlying synchronization mechanism**, making it easy to compare:

- **Code complexity and amount**
- **Performance characteristics**
- **Scalability trade-offs**
- **Maintenance overhead**

## 📝 Implementation Notes

- Each approach is in its own subdirectory with dedicated CMakeLists.txt
- All shared functionality is centralized in the `Common/` directory
- Examples use the same test data and predicates for consistent comparison
- Build targets follow the pattern `Oxygen.OxCo.Examples.BatchExecution.<Approach>`
- No legacy examples exist - all approaches are current and production-ready
