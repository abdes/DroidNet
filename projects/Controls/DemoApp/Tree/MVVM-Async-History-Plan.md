# MVVM & Async History Improvements

This plan lists concise, trackable tasks to implement the recommended MVVM and async history changes for `ProjectLayoutViewModel` and related code in the demo tree module.

## Goals

- Remove blocking calls and make undo/redo changes async-friendly.
- Use idiomatic async/await throughout the ViewModel and history lambdas.
- Improve testability, error handling, and lifecycle safety.

## Tasks

1. Convert blocking helper methods to async
   - Files: `ProjectLayoutViewModel.cs`
   - Change `InsertItemWithVisibility`, `MoveItemWithVisibility`, `EnsureAncestorsExpanded` -> `InsertItemWithVisibilityAsync`, `MoveItemWithVisibilityAsync`, `EnsureAncestorsExpandedAsync`
   - Acceptance: No `.GetAwaiter().GetResult()` usages remain; helpers return `Task` and all callers `await` them.
   - Priority: High
   - Estimate: 1-2 hours

2. Update undo/redo history lambdas to async
   - Files: `ProjectLayoutViewModel.cs`
   - Replace `History.AddChange("...", () => this.DeleteFooAsync(...).GetAwaiter().GetResult())` with `History.AddChange("...", async () => await this.DeleteFooAsync(...).ConfigureAwait(false))`.
   - Acceptance: All `History.AddChange` calls use `Func<Task>` or `AddChangeAsync` if API differs; there are no sync wrappers left.
   - Priority: High
   - Estimate: 1 hour

3. Remove all direct thread-blocking calls
   - Files: `ProjectLayoutViewModel.cs`
   - Replace `newParent.Children.GetAwaiter().GetResult()` and similar with `await newParent.Children.ConfigureAwait(false)` (or appropriate awaitable call) inside async methods.
   - Acceptance: No `GetAwaiter().GetResult()` usages remain in tree demo code.
   - Priority: High
   - Estimate: 30–60 minutes

4. Standardize `ConfigureAwait` usage across the module
   - Files: `ProjectLayoutViewModel.cs`, `ProjectLayoutView.xaml.cs`, adapters, services
   - Rule: Library/VM code uses `ConfigureAwait(false)`; UI/view code may omit it or use `ConfigureAwait(true)` when marshalling to UI context.
   - Acceptance: All async calls follow this rule and doc comment added in `README` or `CONTRIBUTING` for module.
   - Priority: Medium
   - Estimate: 1–2 hours (review + fix)

5. Improve lifecycle / event subscription hygiene
   - Files: `ProjectLayoutViewModel.cs`, `ProjectLayoutView.xaml.cs`
   - Implement `IDisposable` in ViewModel or use weak-event patterns. Ensure `ClipboardContentChanged` and `SelectionModel.PropertyChanged` are unsubscribed.
   - Ensure the view disposes the ViewModel on Unloaded if it owns it, or rely on DI lifetime management.
   - Acceptance: No static or external subscriptions remain after VM/view disposal; add unit test (or manual test) verifying no memory leak.
   - Priority: Medium
   - Estimate: 1-2 hours

6. Improve error reporting and replace `Debug.Fail`
   - Files: `ProjectLayoutViewModel.cs`
   - Replace `Debug.Fail` with a log entry and `OperationError` propagation or explicit exception if surprising/unrecoverable.
   - Acceptance: No `Debug.Fail` used in production code paths; failures are surfaced via logged errors and `OperationError` where appropriate.
   - Priority: Medium
   - Estimate: 1 hour

7. Dialog interactions and UI decoupling (optional but recommended)
   - Files: Add `IDialogService` and a small implementation in view to inject or instantiate for view-model use.
   - Replace `RenameRequested` event if chosen or keep event and add minimal dialog service for testability.
   - Acceptance: ViewModel no longer requires UI types (ContentDialog); rename path can be tested using a mocked dialog service.
   - Priority: Low-Medium
   - Estimate: 2–3 hours

8. Command ergonomics & consistent async commands
   - Files: `ProjectLayoutViewModel.cs`
   - Make sure all async operations are backed by async-capable commands (the code generator's attribute supports async). Ensure `NotifyCanExecuteChanged()` is called in property changed handlers.
   - Acceptance: All async commands are created and invoked consistently and tests exercise CanExecute states for typical flows.
   - Priority: Medium
   - Estimate: 1–2 hours

9. Optional domain refactor

- Files: `ProjectLayoutViewModel.cs`, domain models (Project/Scene/Entity)
- Move `InsertIntoModel`/`RemoveFromModel` into a domain service `IProjectModelService` to decouple view-model and model manipulation.
- Acceptance: Logic moved to domain service; ViewModel orchestrates service calls only; unit tests keep coverage.
- Priority: Low
- Estimate: 4–8 hours
