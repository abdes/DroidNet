# VectorBox Control Specification

`VectorBox` is a styled custom control that provides a clean, XAML-friendly editor for small fixed-size numeric vectors (Vector2/Vector3). It matches the behavior, surface, and conventions of `NumberBox` while coordinating multiple per-component editors.

## Design Goals

1. **Consistency with `NumberBox`**: Share UX patterns—display/edit modes, pointer drag and mouse wheel adjustments, validation, and visual states.
2. **Clean Public API**: Expose per-component writable numeric dependency properties (`XValue`, `YValue`, `ZValue`) and per-component indeterminate presentation flags (`XIsIndeterminate`, `YIsIndeterminate`, `ZIsIndeterminate`). Do **not** expose internal `NumberBox` editor instances as public API.
3. **XAML-friendly**: Provide a concise dependency property surface suitable for XAML binding and MVVM scenarios.
4. **Lightweight and templatable**: Keep the control simple, easy to style, and straightforward to retheme.

## High-Level Behavior

The control always displays per-component editors: two or three `NumberBox` instances (depending on `Dimension`) are hosted in the control's template and serve as the primary UI for viewing and editing values. There is no separate aggregate "display mode"; values are presented and edited through the per-component editors.

**User interactions:**

- Pointer drag or mouse wheel over a component adjusts that component's value (cursor changes during drag via `CustomGrid.InputCursor`).
- Editing clears indeterminate presentation and writes a concrete numeric value to the affected component.
- Validation is performed per-component via a `Validate` event; invalid edits display the same invalid visual state as `NumberBox`.
- Keyboard navigation (Tab/Shift+Tab) moves focus between component editors.

## Implementation Approach

Implement as a styled custom control (partial-class files + `VectorBox.xaml`), **not** as a View/ViewModel pair.

**Rationale:**

- **Consistency**: Aligns with `NumberBox` and the Controls.* family architecture.
- **Templating**: Simplifies styling, retheming, and packaging for consumers.
- **Code reuse**: Leverages existing helpers (`MaskParser`, `CustomGrid`).

## Data Model and Public API

### Design Principles

1. **Non-nullable numeric values**: The control stores non-nullable `float` values for all components (matching `NumberBox` semantics). An `IsIndeterminate` presentation flag is provided when consumers need to display an indeterminate or mixed state.
2. **Dimension support**: Supports both 2D and 3D vectors via a `Dimension` dependency property (values: 2 or 3).
3. **Separation of concerns**: Numeric values, indeterminate presentation, and validation are distinct concerns coordinated through separate DPs.

### Properties

#### Vector and Dimension

- **`VectorValue`** (struct, writable DP) — Aggregate numeric backing for the vector (non-nullable). Holds numeric values for all components. This DP does **not** track whether components are indeterminate (use per-component `*IsIndeterminate` DPs for that). Consumers who only care about numeric values should use `VectorValue` or `GetValues()` and `SetValues()`.
- **`Dimension`** (int, read-only DP; default: 3) — Controls whether the control displays 2 or 3 components (2 or 3). **Recommendation**: Treat as immutable after control creation; changing dimension at runtime is complex and error-prone.

#### Per-Component Numeric DPs

These are **writable proxy properties** that forward to the internal per-component `NumberBox` editor instances. They are **not** independent storage; reads reflect the current numeric value of the corresponding internal editor, and writes propagate through to that editor.

- **`float XValue`** (DP, writable, TwoWay bindable) — Proxy to the X component editor's `NumberValue`.
- **`float YValue`** (DP, writable, TwoWay bindable) — Proxy to the Y component editor's `NumberValue`.
- **`float ZValue`** (DP, writable, TwoWay bindable; only when `Dimension == 3`) — Proxy to the Z component editor's `NumberValue`.

When a per-component DP is set by a consumer (external assignment), the control validates the proposed value, updates the internal editor on success, clears the corresponding `*IsIndeterminate` flag by default, and allows the DP change notification to propagate normally. Consumers monitoring property changes can subscribe via standard WinUI binding and property change notifications.

#### Per-Component Indeterminate Presentation DPs

- **`bool XIsIndeterminate`** (DP, writable) — When true, the X component editor displays `IndeterminateDisplayText` instead of the formatted numeric value. The numeric backing is preserved. Setting to false restores the numeric display.
- **`bool YIsIndeterminate`** (DP, writable) — Same as `XIsIndeterminate` for the Y component.
- **`bool ZIsIndeterminate`** (DP, writable; only when `Dimension == 3`) — Same as `XIsIndeterminate` for the Z component.

These DPs forward to the internal editors' `IsIndeterminate` property (see `NumberBox` documentation for semantics).

#### Formatting and Masking

- **`string ComponentMask`** (DP; default: `"~.#"`) — Default `MaskParser`-style mask applied to all components. See `NumberBox` and `MaskParser` for mask syntax (e.g., `"~.##"` for a signed number with two decimal places).
- **`IDictionary<string, string> ComponentMasks`** (optional, property; keys: "X", "Y", "Z") — Per-component mask overrides. If a component is not in this dictionary, the `ComponentMask` default is used. **Recommendation**: Set this before the control is first rendered (during `OnApplyTemplate`); changing it at runtime requires re-sync of internal editor masks.

#### Control-Level Formatting and Display

- **`string IndeterminateDisplayText`** (DP; default: `"-.-"`) — Text displayed by the internal editors when they are indeterminate. Forwarded to each internal `NumberBox.IndeterminateDisplayText`.

#### Control-Level Label

The following properties control the control-level label (for the entire `VectorBox`, e.g., "Rotation"):

- **`string Label`** (DP) — Label text for the control (e.g., "Rotation").
- **`LabelPosition LabelPosition`** (DP; default: `Left`) — Position of the control-level label relative to the editors (Top, Left, Right, Bottom, None).
- **`TextAlignment HorizontalValueAlignment`** (DP; default: `Center`) — Text alignment of numeric values in the per-component editors.
- **`HorizontalAlignment HorizontalLabelAlignment`** (DP; default: `Left`) — Horizontal alignment of the control-level label.

#### Component Labels

- **`LabelPosition ComponentLabelPosition`** (DP; default: `Left`) — Position of per-component labels ("X", "Y", "Z") relative to each internal editor.
- **`IDictionary<string, LabelPosition> ComponentLabelPositions`** (optional, property; keys: "X", "Y", "Z") — Per-component label position overrides. If a component is not in this dictionary, `ComponentLabelPosition` is used.

By default, each internal editor's `Label` is set to its component name ("X", "Y", "Z").

#### Adjustment Parameters

- **`int Multiplier`** (DP; default: 1) — Scale factor for adjustment increments during keyboard/wheel/drag operations.
- **`bool WithPadding`** (DP; default: false) — Whether to pad numeric components with zeros according to the `ComponentMask`. Forwarded to each internal `NumberBox.WithPadding`.

## Events

### `Validate` Event

```csharp
public event EventHandler<ValidationEventArgs<float>>? Validate;
```

Relayed from internal per-component `NumberBox` editors. Raised when a component's value is being validated:

1. When a user commits an edit to a component (via Enter key, focus loss, or click-away).
2. When a consumer externally sets a per-component DP (e.g., `XValue`).

**Event Flow**: The `VectorBox` subscribes to each internal `NumberBox.Validate` event. When validation is requested, the `VectorBox` passes the event through to consumers by raising its own `Validate` event.

**ValidationEventArgs Target Discrimination**: To enable consumers to apply component-specific validation rules, `ValidationEventArgs<float>` is enhanced with an optional nullable `Target` property:

```csharp
public class ValidationEventArgs<T> : EventArgs
{
  public T? OldValue { get; }
  public T? NewValue { get; }
  public bool IsValid { get; set; } = true;
  public object? Target { get; set; }  // NEW: Can be set to Component enum value (X/Y/Z)
}
```

When `VectorBox` relays validation from internal editors, it sets `Target` to the `Component` enum value (X, Y, or Z) so consumers can discriminate:

```csharp
numberBox.Validate += (s, e) =>
{
  if (e.Target is Component component)
  {
    // Component-specific validation
    e.IsValid = ValidateComponentValue(component, e.NewValue);
  }
};
```

If `Target` is null, the validation applies to a scalar `NumberBox` or other context.

**Validation Responsibility**: Consumers must set `e.IsValid` to indicate whether the proposed value is acceptable. If validation fails, the corresponding component editor (internal `NumberBox`) automatically enters its invalid visual state.

## Public Methods and Convenience Operations

### `SetValues(float x, float y, float z)`

Atomically updates component values in a single operation.

**Behavior:**

1. Updates all provided component values in the internal editors.
2. Updates the per-component proxy DPs (`XValue`, `YValue`, `ZValue`) without re-validating (the control is the authoritative writer in this code path).
3. Clears the corresponding `*IsIndeterminate` flags by default (optional parameter `preserveIndeterminate` can override this).
4. Updates `VectorValue` to reflect all changes.
5. Allows all DP changes to complete and property change notifications to propagate normally.

**Use this method for multi-component updates** rather than setting per-component DPs sequentially. When setting individual DPs, each one fires its own property change notification; `SetValues()` ensures all changes are staged together with a single reentrancy guard.

**Signature variants:**

```csharp
public void SetValues(float x, float y, float z);
public void SetValues(float x, float y, float z, bool preserveIndeterminate);
public void SetValues(float x, float y);  // 2D variant
```

### `float[] GetValues()`

Returns the current numeric values as an array of floats.

**Returns:** An array of length 2 or 3 depending on `Dimension`, containing `[X, Y]` or `[X, Y, Z]`.

**Note**: This returns only numeric values; indeterminate presentation state is not included. Callers should examine the per-component `*IsIndeterminate` DPs separately if needed.

## Synchronization and State Management

### Key Principles

1. **Non-nullable numeric storage**: The control stores non-nullable `float` values at all times.
2. **Independent concerns**: Numeric values, indeterminate presentation, and validation are coordinated but separate.
3. **Reentrancy guarding**: Internal updates use a `_isSyncingValues` guard to prevent feedback loops.
4. **SetCurrentValue usage**: DP updates from code use `SetCurrentValue` to avoid overwriting consumer bindings.

### Per-Component Synchronization

When a consumer externally sets a per-component DP (e.g., `XValue = 5.0f`), the control performs the following steps in the DP's `PropertyChangedCallback`:

1. **Reentrancy check**: If `_isSyncingValues` is true, return early (the change originated from internal synchronization).
2. **Validation**: Capture old and proposed values, raise the `Validate` event (which the `VectorBox` relays to consumers with `Target` set to the component).
3. **On validation failure**:
   - Revert the DP to the old value using `SetCurrentValue` and `_isSyncingValues = true` to suppress feedback.
   - The internal `NumberBox` automatically enters its invalid visual state.
   - Return without updating internal state.
4. **On validation success**:
   - Set `_isSyncingValues = true`.
   - Update the internal editor's numeric backing (write internal `NumberBox.NumberValue`).
   - Clear the corresponding `*IsIndeterminate` flag (via `SetCurrentValue` to avoid binding conflicts).
   - Update `VectorValue` by composing all current component values.
   - Set `_isSyncingValues = false`.
   - Allow the DP change to complete normally (property change notification fires).

**Pseudocode example for `OnXValueChanged`:**

```csharp
private static void OnXValueChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
{
  var ctrl = (VectorBox)d;
  var proposed = (float)e.NewValue;
  var oldVal = (float)e.OldValue;

  // Reentrancy check
  if (ctrl._isSyncingValues) return;

  // Validation is performed by the internal NumberBox editor
  // The VectorBox relays its Validate event to consumers with Target set to Component.X
  var args = new ValidationEventArgs<float>(oldVal, proposed)
  {
    Target = Component.X
  };
  ctrl.OnValidate(args);

  if (!args.IsValid)
  {
    // Revert without triggering validation again
    ctrl._isSyncingValues = true;
    try { ctrl.SetCurrentValue(XValueProperty, oldVal); }
    finally { ctrl._isSyncingValues = false; }

    return;
  }

  // Accept the value and synchronize
  ctrl._isSyncingValues = true;
  try
  {
    ctrl.InternalSetComponentValue(Component.X, proposed);
    ctrl.SetCurrentValue(XIsIndeterminateProperty, false);
    ctrl.UpdateVectorValue();
  }
  finally { ctrl._isSyncingValues = false; }
}
```

### VectorValue and SetValues Synchronization

When `VectorValue` is set externally or `SetValues()` is called:

1. Set `_isSyncingValues = true`.
2. Update all internal editor numeric backings.
3. Update all per-component proxy DPs (`XValue`, `YValue`, `ZValue`) using `SetCurrentValue` to suppress feedback loops.
4. **By default**, clear all per-component `*IsIndeterminate` flags (optional `clearIndeterminate` parameter can preserve them).
5. Update `VectorValue`.
6. Set `_isSyncingValues = false`.
7. Allow property change notifications to propagate normally.

These updates do **not** trigger re-validation (the control is the authoritative writer; external validation has already occurred or is not required).

## Template Structure and Visual States

### Template Parts

The control's default template contains the following required parts:

- **`PartRootGrid`** (type: `CustomGrid`) — Root container for the control. Used for managing the drag cursor during pointer interactions.
- **`PartBackgroundBorder`** (type: `Border`) — Background element behind the control.
- **`PartLabelTextBlock`** (type: `TextBlock`, optional) — Displays the control-level label ("Rotation", etc.).
- **`PartComponentPanel`** (type: `Panel`) — Container that hosts the per-component editors.
- **`PartNumberBoxX`**, **`PartNumberBoxY`**, **`PartNumberBoxZ`** (type: `NumberBox`) — Internal per-component numeric editors. `PartNumberBoxZ` is only required when `Dimension == 3`.

**Important**: These template parts are internal implementation details. Consumers should **not** rely on or directly manipulate them. Use the public proxy DPs (`XValue`, `YValue`, etc.) instead.

**During `OnApplyTemplate`**, the control must:

1. Retrieve all template parts.
2. Wire up event handlers to the internal editors (particularly the `Validate` event for relay to consumers).
3. Configure each editor's `Label` (default: component name), mask, and other properties per the control's DP settings.
4. Re-synchronize all DPs to reflect the template change.

### Visual States

The `VectorBox` itself uses minimal visual states. **Individual validation and edit mode visual states are handled by the internal `NumberBox` controls**, which have their own fine-grained state management.

The control's template uses a single visual state group:

#### `CommonStates` group

- **`Normal`** — Default state when the pointer is not over the control.
- **`Hover`** — Pointer is over the control.
- **`Pressed`** — Pointer is pressed (captured) for dragging.

**Note**: Detailed per-component validation states (ShowingValidValue, ShowingInvalidValue, EditingValidValue, EditingInvalidValue) are **managed by the internal `NumberBox` editors themselves**. The `VectorBox` does not need to replicate this state management. Consumers who need to style validation states at the component level should customize the internal `NumberBox` control template or use WinUI theming.

## User Interaction

### Keyboard

- **Tab / Shift+Tab**: Navigate focus between component editors.
- **Enter**: Commit the current component's edit (runs validation). If validation passes, the numeric value is accepted and `ValuesChanged` is raised.
- **Escape**: Cancel the current edit and restore the previous value.
- **ArrowUp / ArrowDown**: Increment/decrement the focused component (via internal NumberBox behavior).
- **Page Up / Page Down**: Larger increments (via internal NumberBox behavior).

### Pointer (Mouse)

- **Click on a component**: Begin editing that component (enter edit mode).
- **Double-click on a component**: Begin editing with all text selected.
- **Pointer drag on a component**: Adjust that component's numeric value based on horizontal drag delta. The control changes the cursor to `SizeWestEast` during drag (via `CustomGrid.InputCursor`).
- **Mouse wheel over a component**: Increment/decrement the focused component by the NumberBox's default step.

## Parsing, Formatting, and Validation

### Per-Component Masking

The control uses `MaskParser` (from `NumberBox`) for per-component parsing and formatting:

- Each component is parsed independently using its corresponding mask (`ComponentMask` or component-specific override from `ComponentMasks`).
- On display, each component is formatted according to its mask and `WithPadding` setting.
- Parsing errors during edit are caught and the component enters an invalid visual state.

**Mask syntax** follows the `MaskParser` convention (e.g., `"~.##"` for a signed number with two decimal places).

### Validation Flow

**Per-component validation occurs in two scenarios:**

1. **User edits a component** (commits via Enter, focus loss, or interaction):
   - Parse the edited text.
   - Raise the internal `NumberBox.Validate` event (which the `VectorBox` relays to consumers with `Target` set to the component).
   - If validation fails, the internal `NumberBox` automatically enters its invalid visual state and cancels the commit.
   - If validation succeeds, update numeric backing and `VectorValue`, clear `*IsIndeterminate`, and allow property change notifications to propagate normally.

2. **Consumer sets a per-component DP** (e.g., `XValue = 5.0f`):
   - Follow the synchronization logic outlined in the "[Per-Component Synchronization](#per-component-synchronization)" section.

**Validation logic is per-component, not aggregate**: The `Validate` event is relayed separately for each component being changed, with the `Target` property set to discriminate between X, Y, and Z. Consumers must validate each component independently.

## Indeterminate and Mixed-State Presentation

### Core Concept

The control supports an **indeterminate presentation** for each component, useful for scenarios where a single numeric value is not appropriate (e.g., multi-selection UI showing mixed values).

- When `XIsIndeterminate == true`, the X component editor displays `IndeterminateDisplayText` (default: `"-.-"`) instead of the formatted numeric value.
- The numeric backing value (`XValue`) is **preserved** when indeterminate; user edits or external DP assignments automatically clear the indeterminate flag and restore the numeric display.

### Indeterminate Semantics

- **Setting `XIsIndeterminate = true`**: Shows indeterminate display; numeric backing remains intact.
- **Setting `XIsIndeterminate = false`**: Restores numeric display (shows the current `XValue` formatted according to the mask).
- **User editing when indeterminate**: Clears indeterminate and writes the new numeric value.
- **Externally setting `XValue`**: By default, clears `XIsIndeterminate` (can be overridden in `SetValues()`).

### ViewModel Mapping Pattern (Recommended)

For applications with nullable or optional numeric values, use the following pattern to map ViewModel state to the control:

**ViewModel structure:**

```csharp
public class MyViewModel
{
  // Non-nullable backing values
  public float XValue { get; set; }
  public bool IsXIndeterminate { get; set; }

  // Nullable facade property (optional but recommended)
  public float? XNullable
  {
    get => IsXIndeterminate ? (float?)null : XValue;
    set
    {
      if (value.HasValue)
      {
        XValue = value.Value;
        IsXIndeterminate = false;
      }
      else
      {
        IsXIndeterminate = true;
      }
    }
  }
}
```

**View binding (code-behind or Xaml.Interop):**

```csharp
// Option 1: Direct DP binding
<controls:VectorBox
  XValue="{Binding XValue, Mode=TwoWay}"
  XIsIndeterminate="{Binding IsXIndeterminate, Mode=TwoWay}"
  YValue="{Binding YValue, Mode=TwoWay}"
  YIsIndeterminate="{Binding IsYIndeterminate, Mode=TwoWay}"
  ZValue="{Binding ZValue, Mode=TwoWay}"
  ZIsIndeterminate="{Binding IsZIndeterminate, Mode=TwoWay}" />

// Option 2: Code-behind synchronization (when binding is not feasible)
myVectorBox.SetValues(vm.XValue, vm.YValue, vm.ZValue);
myVectorBox.XIsIndeterminate = vm.IsXIndeterminate;
myVectorBox.YIsIndeterminate = vm.IsYIndeterminate;
myVectorBox.ZIsIndeterminate = vm.IsZIndeterminate;
```

This pattern keeps the control implementation simple and allows the ViewModel to coordinate nullable and indeterminate concerns.

## Edge Cases, Constraints, and Considerations

### Dimension Immutability

**Recommendation**: Treat `Dimension` as effectively immutable after control creation. Changing dimension at runtime is complex and error-prone (requires re-templating, re-synchronizing internal editors, updating proxy DPs, etc.).

**Best practice**: Set `Dimension` in XAML or during control initialization, not dynamically during the app lifecycle.

### Culture and Localization

- `MaskParser` currently uses `CultureInfo.InvariantCulture` (via `NumberBox`).
- For localized number formatting, consider adding an optional `CultureInfo Culture` DP that can be passed through to internal editors.
- This is a potential future enhancement but not required for the initial release.

### Clipboard Paste

For improved UX, consider accepting common separators (comma, space, semicolon) when pasting multi-component values into the control:

- E.g., pasting `"1.5, 2.3, 3.7"` into the control could parse as X=1.5, Y=2.3, Z=3.7.
- **Recommendation**: Implement as a convenience feature during edit text parsing, not a core requirement.

### Performance During Drag

Avoid unnecessary allocations during pointer drag operations:

- Update only the affected component during drag (not the entire `VectorValue`).
- Batch internal updates to minimize DP callbacks.
- Use `_isSyncingValues` guard to prevent redundant validation.

## Implementation Roadmap

### File Structure

Follow the `NumberBox` partial-class organization pattern:

- **`VectorBox.cs`** — Core behavior, DP callbacks, synchronization logic, and orchestration.
- **`VectorBox.properties.cs`** — DP registration and CLR property wrappers.
- **`VectorBox.events.cs`** — Event payload types and event-raising helpers.
- **`VectorBox.input.cs`** — Pointer, keyboard, and drag/wheel input handling.
- **`VectorBox.formatting.cs`** — MaskParser integration and per-component formatting.
- **`VectorBox.xaml`** — Default control template and visual state groups (mirrors `NumberBox.xaml` structure but with per-component editors).

### Support Types and Enums

Implement the following types (likely in a separate file or nested in `VectorBox`):

- **`enum Component`** — {X, Y, Z} to identify which component is being discussed.
- **`struct VectorComponentChange`** — Payload for validation events. Fields: Component, OldValue (float), ProposedValue (float).
- **`class VectorValuesChangedEventArgs`** — Payload for `ValuesChanged` events. Includes collection of changed components and their change context.
- **`class ComponentChangeInfo`** — Per-component change context (Component enum, WasIndeterminate flag).

### Optional: Helper and Binder

- **`VectorBoxViewBinder`** (optional) — A small helper/behavior that simplifies ViewModel binding for applications with nullable numeric properties. Demonstrates best practices for coordinating nullable ViewModel state with the control's non-nullable + indeterminate API.

### Testing Strategy

**Unit tests:**

- Verify `SetValues()` atomically updates `VectorValue` and proxy DPs; `ValuesChanged` is raised exactly once.
- Verify user edits validate correctly; invalid edits prevent commit and set invalid visual state.
- Verify per-component DP assignments go through validation and synchronization correctly.
- Test reentrancy guard (`_isSyncingValues`) prevents feedback loops.
- Verify `MaskParser` formatting/parsing for per-component masks (including component-specific overrides).
- Verify indeterminate presentation: setting `*IsIndeterminate` shows indeterminate display without clearing numeric backing.
- Verify `Dimension` changes are handled correctly (or document immutability requirement).
- Test keyboard navigation (Tab/Shift+Tab) and Enter/Escape behavior.

**UI tests:**

- Pointer drag on each component adjusts only that component's value; cursor changes to `SizeWestEast`.
- Mouse wheel increments a component (via internal NumberBox).
- Validation event callbacks can discriminate component via `ValidationEventArgs.Target` and prevent invalid commits.
- Property change notifications fire correctly when DPs are updated.
- `SetValues()` with multiple components stages all changes atomically.

## Framework and Compatibility

- **Target framework**: net9.0-windows10.0.22621.0 (same as InPlaceEdit module).
- **Dependencies**: WinUI 3, existing `MaskParser` and `CustomGrid` from the same module.
- **API naming**: Keep names analogous to `NumberBox` to simplify adoption and reduce learning curve.
- **Visual design**: Match `NumberBox` styling conventions for consistency.

## Future Enhancements (Post-v1.0)

- **Orientation**: Add an `Orientation` DP to allow horizontal or vertical layout of component editors.
- **Localization**: Add optional `CultureInfo` DP for localized number formatting (building on `MaskParser` enhancements).
- **Clipboard integration**: Enhanced paste behavior to accept common separators.
- **Component-level read-only**: Individual per-component read-only flags (e.g., `XReadOnly`, `YReadOnly`, `ZReadOnly`).
- **Custom vector types**: Support for custom numeric vector types (e.g., Unity Vector2Int, custom fixed-size arrays).

---

## Implementation Task List

### Phase 1: Foundation and Core Types

- [x] **Define support types**
  - ✅ Create `enum Component { X, Y, Z }` for identifying components (Component.cs).
  - ✅ **Enhance `ValidationEventArgs<T>`**: Added optional nullable `Target` property to enable component discrimination.

- [x] **Create partial-class file structure**
  - ✅ Create `VectorBox.cs` (core class definition, DP callbacks, synchronization) - ~600 lines, fully implemented.
  - ✅ Create `VectorBox.properties.cs` (DP registration and CLR property wrappers) - ~390 lines, fully implemented.
  - ✅ Create `VectorBox.events.cs` (event relay helpers) - fully implemented.
  - ✅ Create `VectorBox.input.cs` (pointer, keyboard, drag/wheel input handlers) - fully implemented with override methods.
  - ✅ Create `VectorBox.formatting.cs` (MaskParser integration and formatting logic) - fully implemented.

- [x] **Implement dependency properties**
  - ✅ `VectorValue` (struct, writable, triggers sync).
  - ✅ `Dimension` (int, read-only, default: 3).
  - ✅ `ComponentMask` (string, default: `"~.#"`).
  - ✅ `ComponentMasks` (property, optional per-component overrides).
  - ✅ `ComponentLabelPosition` (LabelPosition, default: Left).
  - ✅ `ComponentLabelPositions` (property, optional per-component label overrides).
  - ✅ `XValue`, `YValue`, `ZValue` (float proxies, writable, validation on set).
  - ✅ `XIsIndeterminate`, `YIsIndeterminate`, `ZIsIndeterminate` (bool, writable).
  - ✅ `Label`, `LabelPosition`, `HorizontalValueAlignment`, `HorizontalLabelAlignment` (control-level labeling).
  - ✅ `IndeterminateDisplayText` (string, default: `"-.-"`).
  - ✅ `Multiplier` (int, default: 1).
  - ✅ `WithPadding` (bool, default: false).

- [x] **Implement core synchronization logic**
  - ✅ Add `_isSyncingValues` reentrancy guard field.
  - ✅ Implement `OnApplyTemplate()` to retrieve and set up internal `NumberBox` editors.
  - ✅ Wire up internal `NumberBox.Validate` event relay (set `Target` to `Component` enum value).
  - ✅ Implement DP change callbacks for per-component numeric DPs with full validation and sync logic.
  - ✅ Implement `SetValues(float x, float y, float z)` and overloads for atomic multi-component updates.
  - ✅ Implement `GetValues()` to return current numeric values as array.

### Phase 2: Event System and Validation

- [x] **Implement event relay infrastructure**
  - ✅ Subscribe to each internal `NumberBox.Validate` event.
  - ✅ When validation is requested, create/relay `ValidationEventArgs<float>` with `Target` set to the component.
  - ✅ Expose `event EventHandler<ValidationEventArgs<float>>? Validate` on `VectorBox` for consumer hookup.

- [x] **Implement validation flow**
  - ✅ Wire up per-component DP change handlers to call validation before accepting values.
  - ✅ Implement validation error handling: revert DP, allow internal `NumberBox` to show invalid state.
  - ✅ Ensure validation is skipped during internal sync (reentrancy guard).

- [x] **Implement internal editor synchronization**
  - ✅ Subscribe to internal `NumberBox` value-changed events.
  - ✅ When a component editor value changes, propagate to proxy DPs and aggregate `VectorValue`.
  - ✅ Ensure indeterminate flags are coordinated during internal editor changes.

### Phase 3: Input Handling and Interaction

- [x] **Implement pointer/drag input** (`VectorBox.input.cs`)
  - ✅ Override pointer event handlers (OnPointerMoved, OnPointerPressed, OnPointerReleased).
  - ✅ Input handling delegated to internal `NumberBox` editors (which handle drag detection and value adjustment).
  - ✅ `CustomGrid` root manages `InputCursor` for SizeWestEast during drag.
  - ✅ Mouse wheel handled by internal NumberBox editors.

- [x] **Implement keyboard input** (`VectorBox.input.cs`)
  - ✅ Override OnKeyDown to delegate to internal editors.
  - ✅ Tab/Shift+Tab navigation naturally handled by StackPanel containing editors.
  - ✅ Enter/Escape committed/canceled via internal NumberBox editors.

### Phase 4: Formatting and Display

- [x] **Implement per-component masking** (`VectorBox.formatting.cs`)
  - ✅ Implemented `IsValidMaskValue()` and `FormatMaskValue()` using `MaskParser`.
  - ✅ ApplyComponentProperties applies masks to each editor during OnApplyTemplate.
  - ✅ Component-specific mask overrides supported via `ComponentMasks` dictionary.

- [x] **Implement control-level label positioning**
  - ✅ PartLabelTextBlock positioned and styled via Template binding to Label DP.
  - ✅ HorizontalLabelAlignment controls label alignment.
  - ✅ Label hidden when empty via template.

- [x] **Implement per-component label positioning**
  - ✅ Each internal `NumberBox` editor configured with component name label ("X", "Y", "Z") in SetupNumberBoxParts.
  - ✅ ComponentLabelPosition applied to all editors; per-component overrides via ComponentLabelPositions.

### Phase 5: Visual States and Styling

- [x] **Create XAML control template** (`VectorBox.xaml`)
  - ✅ Define `PartRootGrid` (CustomGrid) for drag cursor management.
  - ✅ Define `PartBackgroundBorder` (Border).
  - ✅ Define `PartLabelTextBlock` (TextBlock, optional).
  - ✅ Define `PartComponentPanel` (StackPanel) to host `PartNumberBoxX`, `PartNumberBoxY`, `PartNumberBoxZ`.
  - ✅ Define internal `NumberBox` editors with proper naming and configuration.
  - ✅ Implement common visual states (Normal, Hover, Pressed).
  - ✅ Per-component validation states managed by internal NumberBox editors.

- [x] **Implement visual state management**
  - ✅ Component validity states tracked by internal NumberBox editors.
  - ✅ Invalid visual states set automatically by NumberBox on validation failure.
  - ✅ Pointer capture state (Pressed vs. Normal/Hover) handled by CommonStates visual state group.

### Phase 6: Testing and Documentation

- [ ] **Implement unit tests**
  - Test `SetValues()` atomic updates: verify `VectorValue` is updated, proxy DPs are synced, `ValuesChanged` raised exactly once.
  - Test per-component DP validation: invalid values rejected, visual state updated, no `ValuesChanged` raised.
  - Test successful per-component DP updates: validation passes, numeric backing updated, `*IsIndeterminate` cleared, `ValuesChanged` raised.
  - Test reentrancy guard: confirm no feedback loops during sync.
  - Test MaskParser integration: per-component formatting and parsing works correctly.
  - Test component-specific mask overrides.
  - Test indeterminate presentation: setting `*IsIndeterminate` shows indeterminate display without losing numeric backing.
  - Test `GetValues()` and `SetValues()` with different `Dimension` values.
  - Test `Dimension` immutability (document expected behavior).

- [ ] **Implement UI tests**
  - Test pointer drag adjusts correct component value; cursor changes to SizeWestEast.
  - Test mouse wheel increments by `Step`.
  - Test keyboard navigation (Tab/Shift+Tab) between component editors.
  - Test keyboard increments (Arrow keys, Page Up/Down).
  - Test Enter commits; Escape cancels.
  - Test validation event prevents invalid commits and shows invalid visual state.
  - Test indeterminate presentation updates.
  - Test label positioning for both control-level and per-component labels.

- [ ] **Create usage examples and documentation**
  - Create README.md (mirrors NumberBox README structure).
  - Provide XAML binding examples.
  - Provide ViewModel mapping pattern example (nullable to non-nullable + indeterminate).
  - Document edge cases (dimension immutability, culture, performance).

### Phase 7: Optional Enhancements (v1.1+)

- [ ] **VectorBoxViewBinder helper** (optional)
  - Create a small attached behavior or utility class to simplify ViewModel binding for nullable numeric properties.
  - Demonstrate automatic coordination of nullable ViewModel state with control's non-nullable + indeterminate API.

- [ ] **Culture and localization support**
  - Add `CultureInfo Culture` DP and pass through to internal editors.

- [ ] **Clipboard multi-value paste**
  - Enhance edit parsing to accept common separators (comma, space, semicolon).

---

## Implementation Notes

### Key Synchronization Patterns

1. **Per-component DP set** → Validate (relay via `Validate` event with `Target`) → Update internal editor → Update proxy DPs → Update `VectorValue` → Allow property change notifications.
2. **`SetValues()` call** → Update all internal editors → Update all proxy DPs → Update `VectorValue` → Allow property change notifications.
3. **Internal editor value change** → Validate (relay via `Validate` event) → Propagate to proxy DPs → Update `VectorValue` → Allow property change notifications.

All synchronization must use `_isSyncingValues` guard to prevent feedback loops.

### Critical Implementation Details

- **Event relay strategy**: The `VectorBox` subscribes to internal `NumberBox.Validate` events and re-exposes them via its own `Validate` event. When relaying, set `ValidationEventArgs<float>.Target` to the `Component` enum value so consumers can discriminate.
- **SetCurrentValue usage**: Use `SetCurrentValue` instead of `SetValue` when updating DPs from code to preserve consumer bindings.
- **Proxy DP semantics**: `XValue`, `YValue`, `ZValue` are proxy properties that forward to internal editor instances; they are not independent storage.
- **Indeterminate independence**: Numeric values and indeterminate flags are independent; setting a numeric value does not automatically clear indeterminate (by design), but the default behavior clears it.
- **Per-component validation**: Validation occurs per-component via the relayed `Validate` event; there is no aggregate vector-level validation.
- **Visual state delegation**: The `VectorBox` control itself only manages `CommonStates` (Normal, Hover, Pressed). **Validation and edit mode visual states are managed entirely by the internal `NumberBox` controls**, which already have robust state management.

### Template Part Names (Use as Constants)

- `PartRootGrid`
- `PartBackgroundBorder`
- `PartLabelTextBlock`
- `PartComponentPanel`
- `PartNumberBoxX`
- `PartNumberBoxY`
- `PartNumberBoxZ`

### Visual State Group Names

- `CommonStates` (Normal, Hover, Pressed) — controlled by VectorBox
- Internal `NumberBox` editors manage their own ValueStates independently
