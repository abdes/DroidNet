# SceneQuery Batch Collection

## Architecture Overview

The batch query system coordinates multiple query operations during a single scene traversal using **BroadcastChannel** pattern and **coroutine combinators**.

### Operation Composition Mechanism

**Single Traversal, Multiple Operations**:
1. User calls `ExecuteBatch()` with a lambda containing multiple batch operations (FindFirst, Collect, Count, Any)
2. Each batch operation **registers** itself with BatchQueryExecutor, creating a coroutine that reads from the same BroadcastChannel
3. Traversal coroutine **streams** each visited node to the BroadcastChannel
4. BroadcastChannel **fans out** each node to ALL registered operation coroutines simultaneously
5. All operations process the same node stream during a single tree walk

**Result**: One traversal feeds multiple consumers through broadcast channel fan-out.

### Key Components

1. **BatchQueryExecutor**: Coordinates operations using BroadcastChannel to distribute nodes
2. **Operation Coroutines**: Each operation (FindFirst, Collect, Count, Any) runs as separate coroutine reading from shared channel
3. **Traversal Coroutine**: Streams nodes via visitor pattern with early termination check
4. **Coroutine Coordination**: `AnyOf(traversal, AllOf(operations))` handles completion

### Critical Design Principles

**DO NOT BREAK THESE:**

- **Visitor Early Termination Check**: The visitor checks if all operations complete after each node and returns `VisitResult::kStop`. This prevents unnecessary child collection/queuing when all operations finish early. **This is NOT redundant** - it provides immediate traversal control vs eventual coroutine coordination.

- **Operation Types Matter**:
  - `FindFirst`/`Any`: Can terminate early when match found
  - `Collect`/`Count`: Must see ALL nodes to complete correctly
  - Batch continues until ALL operations complete OR traversal finishes

- **Reference-Based Output**: Batch operations use reference parameters (`std::optional<SceneNode>&`, `std::vector<SceneNode>&`) for zero-copy semantics

- **Status Updates**: Operations update status immediately when complete (e.g., FindFirst finds match) to enable visitor early termination

### Common Mistakes to Avoid

1. **Don't remove visitor early termination check** - it's an optimization, not redundancy
2. **Don't assume all operations can terminate early** - Collect/Count need full traversal
3. **Don't ignore the coroutine infrastructure** - use existing AnyOf/AllOf combinators
4. **Don't break reference semantics** - operations populate caller's variables directly
