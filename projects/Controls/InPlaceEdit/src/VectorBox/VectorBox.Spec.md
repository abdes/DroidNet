# VectorBox Control Specification

`VectorBox` is a styled custom control that provides a clean, XAML-friendly editor for small fixed-size numeric vectors of two, three or four components: positions and scales, quaternions, plane equations, tangents with handedness, and colors with alpha. It matches the behavior, surface, and conventions of `NumberBox` while coordinating multiple per-component editors.

## Design Goals

1. **Consistency with `NumberBox`**: Share UX patterns—display/edit modes, pointer scrubbing on labels and values, validation, and visual states. The mouse wheel remains available for scrolling the containing view.
2. **Clean Public API**: Expose per-component writable numeric dependency properties (`XValue` to `WValue`) and per-component indeterminate presentation flags (`XIsIndeterminate` to `WIsIndeterminate`). Do **not** expose internal `NumberBox` editor instances as public API.
3. **XAML-friendly**: Provide a concise dependency property surface suitable for XAML binding and MVVM scenarios.
4. **Lightweight and templatable**: Keep the control simple, easy to style, and straightforward to retheme.

## High-Level Behavior

The control always displays per-component editors: two to four `NumberBox` instances (depending on `Dimension`) are hosted in the control's template and serve as the primary UI for viewing and editing values. There is no separate aggregate "display mode"; values are presented and edited through the per-component editors.

**User interactions:**

- Horizontal pointer drag on a visible component label, or on a component value, adjusts that component's value (cursor changes during drag via `CustomGrid.InputCursor`). A click on a value enters text edit. Each component's `NumberBox` does the scrubbing; `VectorBox` has no pointer code of its own.
- Mouse-wheel input scrolls the containing view and never changes a component value.
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

1. **Non-nullable numeric values**: The control stores non-nullable `double` values for all components (matching `NumberBox` semantics). Consumers that store single precision narrow once, where they apply the value to their model. An `IsIndeterminate` presentation flag is provided when consumers need to display an indeterminate or mixed state.
2. **Dimension support**: Supports 2D, 3D and 4D vectors via a `Dimension` dependency property (values: 2, 3 or 4). Internally every component is one entry of a component table (editor, label, value and indeterminate properties), so no behavior is written per axis.
3. **Separation of concerns**: Numeric values, indeterminate presentation, and validation are distinct concerns coordinated through separate DPs.

### Properties

#### Vector and Dimension

- **`Dimension`** (int DP; default: 3; `MinimumDimension` 2 to `MaximumDimension` 4) — The number of components shown. Other values show the nearest supported number. Components beyond `Dimension` keep their values, so changing `Dimension` never loses one. It can change at any time: the control shows or hides component editors and lays them out again without recreating them.

#### Per-Component Numeric DPs

These are **writable proxy properties** that forward to the internal per-component `NumberBox` editor instances. They are **not** independent storage; reads reflect the current numeric value of the corresponding internal editor, and writes propagate through to that editor.

- **`double XValue`** (DP, writable, TwoWay bindable) — Proxy to the X component editor's `NumberValue`.
- **`double YValue`** (DP, writable, TwoWay bindable) — Proxy to the Y component editor's `NumberValue`.
- **`double ZValue`** (DP, writable, TwoWay bindable; shown when `Dimension >= 3`) — Proxy to the Z component editor's `NumberValue`.
- **`double WValue`** (DP, writable, TwoWay bindable; shown when `Dimension == 4`) — Proxy to the W component editor's `NumberValue`, such as a quaternion's scalar part or a color's alpha.

When a per-component DP is set by a consumer (external assignment), the control validates the proposed value, updates the internal editor on success, clears the corresponding `*IsIndeterminate` flag by default, and allows the DP change notification to propagate normally. Consumers monitoring property changes can subscribe via standard WinUI binding and property change notifications.

#### Per-Component Indeterminate Presentation DPs

- **`bool XIsIndeterminate`** (DP, writable) — When true, the X component editor displays `IndeterminateDisplayText` instead of the formatted numeric value. The numeric backing is preserved. Setting to false restores the numeric display.
- **`bool YIsIndeterminate`** (DP, writable) — Same as `XIsIndeterminate` for the Y component.
- **`bool ZIsIndeterminate`** (DP, writable) — Same as `XIsIndeterminate` for the Z component.
- **`bool WIsIndeterminate`** (DP, writable) — Same as `XIsIndeterminate` for the W component.

These DPs forward to the internal editors' `IsIndeterminate` property (see `NumberBox` documentation for semantics).

#### Formatting and Masking

- **`string ComponentMask`** (DP; default: `"~.#"`) — Default `MaskParser`-style mask applied to all components. See `NumberBox` and `MaskParser` for mask syntax (e.g., `"~.##"` for a signed number with two decimal places).
- **`IDictionary<Component, string> ComponentMasks`** (optional, property; keys: `Component.X` to `Component.W`) — Per-component mask overrides. If a component is not in this dictionary, the `ComponentMask` default is used. **Recommendation**: Set this before the control is first rendered (during `OnApplyTemplate`); changing it at runtime requires re-sync of internal editor masks.

#### Control-Level Formatting and Display

- **`string IndeterminateDisplayText`** (DP; default: `"-.-"`) — Text displayed by the internal editors when they are indeterminate. Forwarded to each internal `NumberBox.IndeterminateDisplayText`.
- **`Style? ComponentStyle`** (DP; default: `null`) — Optional style for all component NumberBoxes. The default template uses `DefaultNumberBoxStyle` when unset. Use a style based on that standard style to adjust borders, corner radii, padding and minimum height without replacing either control template. The standard VectorBox template respects `Padding` and fits its component fields to the available width.
- **`bool AutoStackComponents`** (DP; default: `false`) — Opt in to vertical component editors when the available control width cannot accommodate the measured horizontal group. Measured without a width limit, the control asks for that one-row width, so a container that probes its natural width, such as a property row, learns where the components would stack.
- **`double ComponentMinimumWidth`** (DP; default: `80`) — Minimum usable channel width at normal text size. The policy includes component spacing, padding, an optional control label, system text scaling and measured component requirements. `AreComponentsStacked` reports the chosen layout. Resizing does not recreate editors or interrupt edit sessions.
- An empty control label reserves no column or gap, regardless of `LabelPosition`. Each component's automation name includes the group's `AutomationProperties.Name` and its channel label; include the property's unit or color-space annotation in the group name.

#### Control-Level Label

The following properties control the control-level label (for the entire `VectorBox`, e.g., "Rotation"):

- **`string Label`** (DP) — Label text for the control (e.g., "Rotation").
- **`LabelPosition LabelPosition`** (DP; default: `Left`) — Position of the control-level label relative to the editors (Top, Left, Right, Bottom, None).
- **`TextAlignment HorizontalValueAlignment`** (DP; default: `Center`) — Text alignment of numeric values in the per-component editors.
- **`HorizontalAlignment HorizontalLabelAlignment`** (DP; default: `Left`) — Horizontal alignment of the control-level label.

#### Component Labels

- **`LabelPosition ComponentLabelPosition`** (DP; default: `None`) — Position of optional per-component labels. `None` omits them. Each label is the component `NumberBox`'s own label, so it scrubs wherever it is placed; `VectorBox` draws no label of its own.
- **`bool CompactComponentLabels`** (DP; default: `false`) — Sets each component's `NumberBox.IsCompact`: a left or right label shows inside the value field and the input uses the remaining width. Top and bottom labels are never compact.
- **`IDictionary<Component, LabelPosition> ComponentLabelPositions`** (optional, property; keys: `Component.X` to `Component.W`) — Per-component label position overrides. If a component is not in this dictionary, `ComponentLabelPosition` is used.
- **`IDictionary<Component, string> ComponentLabels`** (optional, property; keys: `Component.X` to `Component.W`) — Per-component text overrides, e.g. `R`, `G`, `B` and `A` for a color vector. Defaults are empty; labels are opt-in through a non-`None` position.
- **`IDictionary<Component, Brush> ComponentLabelForegrounds`** (optional, property; keys: `Component.X` to `Component.W`) — Optional per-component label foregrounds, such as red, green, and blue axis colors. Configure label dictionaries before template application.

#### Adjustment Parameters

- **`double Multiplier`** (DP; default: 1) — The change one DIP of horizontal drag makes to a component; see the NumberBox `Multiplier`.
- **`double Minimum`**, **`double Maximum`** (DPs; default: unbounded) — The range of user input for every component, enforced by each NumberBox. `SetComponentRange(Component, minimum, maximum)` gives one component its own range, such as 0 to 1 for alpha, and `ClearComponentRange` removes it; both take effect at once.
- **`bool WithPadding`** (DP; default: false) — Whether to pad numeric components with zeros according to the `ComponentMask`. Forwarded to each internal `NumberBox.WithPadding`.

## Events

### `Validate` Event

```csharp
public event EventHandler<ValidationEventArgs<double>>? Validate;
```

Relayed from internal per-component `NumberBox` editors. Raised when a component's value is being validated:

1. When a user commits an edit to a component (via Enter key, focus loss, or click-away).
2. When a consumer externally sets a per-component DP (e.g., `XValue`).

**Event Flow**: The `VectorBox` subscribes to each internal `NumberBox.Validate` event. When validation is requested, the `VectorBox` passes the event through to consumers by raising its own `Validate` event.

**ValidationEventArgs Target Discrimination**: To enable consumers to apply component-specific validation rules, `ValidationEventArgs<T>` has an optional nullable `Target` property:

```csharp
public class ValidationEventArgs<T> : EventArgs
{
  public T? OldValue { get; }
  public T? NewValue { get; }
  public bool IsValid { get; set; } = true;
  public object? Target { get; set; }  // The Component (X to W) a VectorBox relays for
}
```

When `VectorBox` relays validation from internal editors, it sets `Target` to the `Component` enum value (X to W) so consumers can discriminate:

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

### `SetValues(ReadOnlySpan<double> values, bool preserveIndeterminate = false)`

Atomically updates the shown components in a single operation. `values` holds one value per shown component, in X, Y, Z, W order; any other count throws `ArgumentException`.

**Behavior:**

1. Updates all provided component values in the internal editors.
2. Updates the per-component proxy DPs (`XValue` to `WValue`) without re-validating (the control is the authoritative writer in this code path).
3. Clears the corresponding `*IsIndeterminate` flags by default (`preserveIndeterminate` keeps them).
4. Allows all DP changes to complete and property change notifications to propagate normally.

**Use this method for multi-component updates** rather than setting per-component DPs sequentially. When setting individual DPs, each one fires its own property change notification; `SetValues()` ensures all changes are staged together with a single reentrancy guard.

### `double[] GetValues()`

Returns the current numeric values.

**Returns:** One value per shown component: `[X, Y]`, `[X, Y, Z]` or `[X, Y, Z, W]` depending on `Dimension`.

**Note**: This returns only numeric values; indeterminate presentation state is not included. Callers should examine the per-component `*IsIndeterminate` DPs separately if needed.

## Synchronization and State Management

### Key Principles

1. **Non-nullable numeric storage**: The control stores non-nullable `double` values at all times.
2. **Independent concerns**: Numeric values, indeterminate presentation, and validation are coordinated but separate.
3. **Reentrancy guarding**: Internal updates use an `isSyncingValues` guard to prevent feedback loops.
4. **DP update strategy**: The implementation uses DP setters (`SetValue` via the CLR property setters) together with the `isSyncingValues` reentrancy guard when performing programmatic updates. This prevents feedback loops while preserving expected binding behavior.

### Per-Component Synchronization

When a consumer externally sets a per-component DP (for example `XValue = 5.0`), the control follows the implementation's synchronization and validation pattern:

1. Reentrancy check: if `isSyncingValues` is true, the change is ignored because it originated from an internal sync.
2. Validation: the control creates a `ValidationEventArgs<double>` with the old and proposed values, sets `Target` to the corresponding `Component` enum, and calls `OnValidate(args)` to relay the request to the consumer. Consumers must set `args.IsValid`.
3. On validation failure: the control reverts the DP to the old value (the implementation uses `SetValue(...)` while respecting the reentrancy guard) and returns; the internal `NumberBox` will show the invalid visual state.
4. On validation success: the control sets `isSyncingValues = true`, updates the internal `NumberBox.NumberValue`, clears the corresponding `*IsIndeterminate` flag via the DP setter, and finally clears the reentrancy guard.

The concrete implementation in `VectorBox` uses an instance method to handle these transitions (non-static) and follows a try/finally pattern to ensure the `isSyncingValues` guard is always reset.

### SetValues Synchronization

When `SetValues()` is called:

1. Set `isSyncingValues = true`.
2. Update all internal editor numeric backings.
3. Update all per-component proxy DPs (`XValue` to `WValue`) via the DP setters while the `isSyncingValues` reentrancy guard is active to avoid triggering validation callbacks.
4. **By default**, clear all per-component `*IsIndeterminate` flags (`preserveIndeterminate` keeps them).
5. Set `isSyncingValues = false`.
6. Allow property change notifications to propagate normally.

These updates do **not** trigger re-validation (the control is the authoritative writer; external validation has already occurred or is not required).

## Template Structure and Visual States

### Template Parts

The control's default template contains the following required parts:

- **`PartRootGrid`** (type: `CustomGrid`) — Root container for the control. Used for managing the drag cursor during pointer interactions.
- **`PartBackgroundBorder`** (type: `Border`) — Background element behind the control.
- **`PartLabelTextBlock`** (type: `TextBlock`, optional) — Displays the control-level label ("Rotation", etc.).
- **`PartComponentPanel`** (type: `Panel`) — Container that hosts the per-component editors.
- **`PartNumberBoxX`** to **`PartNumberBoxW`** (type: `NumberBox`) — Internal per-component editors, which also show the component labels, each in its own grid inside `PartComponentPanel`. The control collapses the grids of components beyond `Dimension`.

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
- **Enter**: Commit the current component's edit (runs validation). If validation passes, the numeric value is accepted.
- **Escape**: Cancel the current text edit, restoring the previous value. During a scrub, end it as cancelled, so the owner can restore the value the scrub began with.

### Pointer (Mouse)

- **Click on a value**: Enter text edit for that component, with all text selected.
- **Pointer drag on a visible component label**: Scrubs that component from the press, one `Multiplier` step per DIP of horizontal travel (Shift for a tenth). The cursor changes to `SizeWestEast` during the drag (via `CustomGrid.InputCursor`).
- **Pointer drag on a value**: Scrubs the same way once the pointer has moved 4 DIPs sideways; the click that ends the drag does not enter text edit.
- **Mouse wheel**: Scrolls the containing view and does not edit values.

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
   - If validation succeeds, update the numeric backing, clear `*IsIndeterminate`, and allow property change notifications to propagate normally.

2. **Consumer sets a per-component DP** (e.g., `XValue = 5.0`):
   - Follow the synchronization logic outlined in the "[Per-Component Synchronization](#per-component-synchronization)" section.

**Validation logic is per-component, not aggregate**: The `Validate` event is relayed separately for each component being changed, with the `Target` property set to discriminate between components. Consumers must validate each component independently.

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
  public double XValue { get; set; }
  public bool IsXIndeterminate { get; set; }

  // Nullable facade property (optional but recommended)
  public double? XNullable
  {
    get => IsXIndeterminate ? null : XValue;
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
myVectorBox.SetValues([vm.XValue, vm.YValue, vm.ZValue]);
myVectorBox.XIsIndeterminate = vm.IsXIndeterminate;
myVectorBox.YIsIndeterminate = vm.IsYIndeterminate;
myVectorBox.ZIsIndeterminate = vm.IsZIndeterminate;
```

This pattern keeps the control implementation simple and allows the ViewModel to coordinate nullable and indeterminate concerns.

## Edge Cases, Constraints, and Considerations

### Dimension Changes

`Dimension` can change at any time. The template always holds all four editors; the control collapses those beyond `Dimension`, applies label presentation to the shown ones and measures the layout again. Values and indeterminate flags of hidden components are kept, and edit sessions of shown components are not interrupted.

### Culture and Localization

- `MaskParser` currently uses `CultureInfo.InvariantCulture` (via `NumberBox`).
- For localized number formatting, consider adding an optional `CultureInfo Culture` DP that can be passed through to internal editors.
- This is a potential future enhancement but not required for the initial release.

### Clipboard Paste

For improved UX, consider accepting common separators (comma, space, semicolon) when pasting multi-component values into the control:

- E.g., pasting `"1.5, 2.3, 3.7, 1"` into a 4D control could parse as X=1.5, Y=2.3, Z=3.7, W=1.
- **Recommendation**: Implement as a convenience feature during edit text parsing, not a core requirement.

## Source Files

- **`VectorBox.cs`** — Core behavior, DP callbacks, synchronization, layout and orchestration.
- **`VectorBox.properties.cs`** — DP registration and CLR property wrappers.
- **`VectorBox.events.cs`** — Event relay helpers.
- **`VectorBox.input.cs`** — Keyboard input routing; each component `NumberBox` owns pointer scrubbing.
- **`VectorBox.formatting.cs`** — MaskParser integration and per-component formatting.
- **`ComponentMap`1.cs`** — The per-component dictionaries, which apply a change at once, even after the template is applied.
- **`VectorBox.xaml`** — Default control template and visual state groups.

Tests live in `Controls/InPlaceEdit/tests` (`VectorBoxDimensionTests`, `VectorBoxLabelTests`) and exercise realized controls; scrubbing decisions are tested through `ScrubGestureTests`.

## Framework and Compatibility

- **Dependencies**: WinUI 3, existing `MaskParser` and `CustomGrid` from the same module.
- **API naming**: Keep names analogous to `NumberBox` to simplify adoption and reduce learning curve.
- **Visual design**: Match `NumberBox` styling conventions for consistency.

## Future Enhancements (Post-v1.0)

- **Orientation**: Add an `Orientation` DP to allow horizontal or vertical layout of component editors.
- **Localization**: Add optional `CultureInfo` DP for localized number formatting (building on `MaskParser` enhancements).
- **Clipboard integration**: Enhanced paste behavior to accept common separators.
- **Component-level read-only**: Individual per-component read-only flags (e.g., `XReadOnly` to `WReadOnly`).
- **Custom vector types**: Support for custom numeric vector types (e.g., Unity Vector2Int, custom fixed-size arrays).
