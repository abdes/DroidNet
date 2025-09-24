# DroidNet In-Place Editable Controls

This library provides a set of lightweight, reusable controls for editing values
directly in the UI. The controls are designed to integrate with MVVM patterns
and XAML-based applications, and follow common UI conventions for selection,
editing, validation and accessibility.

Contents

- In-Place Editable Label
- Number Box
- Vector Box
- Common features & events
- Styling & theming
- Accessibility
- Examples
- License

## In-Place Editable Label

An `InPlaceEditableLabel` is a display control that shows a read-only value
(rendered as a `TextBlock`) and allows the user to edit it in place. The control
supports two editing modes:

- Inline replacement: the `TextBlock` is replaced by a `TextBox` at the same
  location while the user edits.
- Overlay (flyout): a `TextBox` overlay or flyout is shown above the label for
  editing.

Key features

- Double-click (or programmatic command) to begin editing.
- Optional validation pipeline that can run on each keystroke (partial
  validation) or when the edit completes (full validation).
- Configurable commit/cancel behavior (e.g., Enter commits, Esc cancels).
- Visual feedback for validation failures (error state styling).
- Customizable edit template and flyout placement.

Typical properties

- `Text` — backing value displayed by the label.
- `IsEditing` — read/write state indicating whether the control is in edit mode.
- `EditMode` — inline or flyout overlay.
- `ValidationMode` — partial or full validation.

Events

- `Validate` — raised when the text changes (partial) or when editing completes
  (full). The handler can accept or reject the change.
- `EditStarted` / `EditEnded` — lifecycle events for edit sessions.

## Number Box

`NumberBox` is a numeric editor that displays a formatted numeric value (by
default as a `TextBlock`) and supports in-place editing via the same inline or
overlay patterns used by the label.

Key features

- Numeric input and parsing for `float` (and optional other numeric types via
  customization).
- Optional unit suffix displayed alongside the value.
- Value adjustment via mouse drag or scroll wheel using a configurable increment
  step.
- In-place editing using `TextBox` with validation support.
- Validation prevents committing invalid numeric values when using drag/scroll
  increments; the flyout editing will display validation errors visually and
  prevent commit until valid.

Typical properties

- `Value` — numeric backing value (e.g. `float`).
- `Unit` — optional unit string shown after the value (e.g., "px", "%").
- `Step` — increment used for mouse-driven adjustments.
- `Minimum` / `Maximum` — optional bounds.

Events

- `Validate` — similar to the label control; used to accept or reject proposed
  value changes.
- `ValueChanged` — raised when the numeric value is committed.

Usage notes

- Use the `Step` property to control sensitivity when adjusting via mouse
  movement or scroll wheel.
- Partial validation is used while changing via mouse/scroll; the control will
  only apply changes when valid.

## Vector Box

`VectorBox` is a composite control for editing an ordered set of numeric values
(for example X/Y/Z or a color quadruplet). The control renders a sequence of
`NumberBox` instances either inline in a row or stacked in a column.

Key features

- Reuses `NumberBox` semantics for each component.
- Supports per-component validation and a global `Validate` event.
- Configurable layout (horizontal or vertical), spacing, and headers for each
  component.

Typical properties

- `Values` — collection of numeric values bound to the UI.
- `Orientation` — `Horizontal` or `Vertical`.

## Common features & events

Validation

- All in-place editors expose a `Validate` event (or delegate) that the host
  application can use to implement business rules.
- Validation can be partial (on every keystroke) or full (on commit) depending
  on the control and user settings.
- When validation fails, the controls provide a consistent visual state to
  indicate the error.

Editing lifecycle

- Controls expose start/end editing events and provide programmatic methods to
  begin or cancel editing.
- Default keyboard behavior typically follows platform conventions: `Enter` to
  commit, `Esc` to cancel.

Data binding & MVVM

- All controls were built with XAML data binding in mind. Bind the `Text`,
  `Value` or `Values` properties to view model properties and handle `Validate`
  or `ValueChanged` events in the view model via commands or event handlers.

Notifications

- Controls will raise property-change notifications for relevant dependency or
  bindable properties so UI frameworks and view models can react.

## Styling & Theming

- Controls are template-driven and expose visual states for normal, focussed and
  validation-error states.
- You can override control templates and style the `TextBlock`, `TextBox`, or
  the overlay flyout to match your application's theme.
- Use the normal resource mechanisms (implicit styles, theme dictionaries) to
  provide custom visuals.

## Accessibility

- Controls expose appropriate automation properties for screen readers (e.g.,
  name and value).
- Keyboard navigation and focus management follow platform guidelines: tabbing
  goes into the editor and keyboard commands can commit/cancel edits.
- Ensure any custom templates preserve logical focus and automation peers for
  best accessibility behavior.

## Examples

Inline label editing (XAML)

```xaml
<controls:InPlaceEditableLabel
    Text="Hello world"
    EditMode="Inline"
    ValidationMode="Full"
    Validate="OnLabelValidate" />
```

NumberBox with unit and step (XAML)

```xaml
<controls:NumberBox
    Value="42"
    Unit="px"
    Step="0.5"
    Minimum="0"
    Maximum="100"
    Validate="OnNumberValidate" />
```

VectorBox example (XAML)

```xaml
<controls:VectorBox
    Values="{Binding PositionValues}"
    Orientation="Horizontal"
    Validate="OnVectorValidate" />
```

## Troubleshooting & tips

- If validation handlers are not firing, ensure the event is wired up in XAML or
  code-behind and that bindings are correct.
- When templating controls, remember to keep the `TextBox` element with the
  expected name if the control template expects it for focus management.
- For high-frequency changes (e.g. mouse drag on `NumberBox`), prefer partial
  validation that is cheap; use full validation on commit for expensive checks.

## License

These controls are distributed under the MIT license. See `LICENSE` in the
repository root for details.

---

For more advanced scenarios and API reference, consult the source code and XML
documentation compiled into the assemblies.
