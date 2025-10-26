# Controls.InPlaceEdit

Lightweight, XAML-friendly in-place editors for WinUI-style applications.

This package provides three primary controls designed for MVVM-friendly UIs:

- InPlaceEditableLabel — read-only text that can be edited inline or in an overlay.
- NumberBox — a compact, editable numeric field with drag/scroll adjustments and validation.
- VectorBox — a composite of per-component NumberBox editors for Vector2/Vector3-style values.

> [!NOTE]
> This README summarizes the public surface and common usage. For the authoritative API, consult the XML docs and the control source (`src/`).

## Highlights

- Small, templatable controls that follow the same UX patterns (display vs edit, validation, visual states) used across the Controls.* family.
- Validation is event-driven: each control exposes a `Validate` event that allows the host to accept/reject proposed values.
- Number adjustments via pointer drag and mouse wheel (configurable step/multiplier).
- VectorBox coordinates multiple NumberBox instances and exposes per-component proxy dependency properties and indeterminate presentation flags.

## Quick start

1. Add the project/assembly to your solution and reference `Controls.InPlaceEdit`.
2. Include the default theme (the assembly provides `Themes/Generic.xaml`).
3. Use the controls in XAML.

Example — inline label

```xaml
<controls:InPlaceEditableLabel
    Text="Hello world"
    EditMode="Inline"
    ValidationMode="Full"
    Validate="OnLabelValidate" />
```

Example — NumberBox

```xaml
<controls:NumberBox
    Value="42"
    Unit="px"
    Step="0.5"
    Minimum="0"
    Maximum="100"
    Validate="OnNumberValidate" />
```

Example — VectorBox (3D)

```xaml
<controls:VectorBox
    Dimension="3"
    XValue="0"
    YValue="1"
    ZValue="2"
    ComponentMask="~.#"
    Validate="OnVectorValidate" />
```

## API summary (most-used surface)

### NumberBox

- Value (float) — numeric backing value
- Step, Minimum, Maximum — adjustment and bounds
- Unit — optional suffix
- Validate event — raised on commit/validation

### VectorBox

- Dimension (2 or 3)
- VectorValue — aggregate struct backing
- XValue, YValue, ZValue — per-component TwoWay proxy DPs
- XIsIndeterminate, YIsIndeterminate, ZIsIndeterminate — presentation flags
- ComponentMask / ComponentMasks — formatting mask(s)
- IndeterminateDisplayText — text shown when indeterminate
- Validate event — relayed with a `Target` (component) to indicate which component is being validated
- Methods: SetValues(...), GetValues()

### InPlaceEditableLabel

- Text
- IsEditing, EditMode
- Validate, EditStarted, EditEnded events

For more details see the source files under `src/` (for example `VectorBox.*`, `NumberBox.*`, `InPlaceEditableLabel.*`).

## Styling & templating

All controls are template-driven. The default styles and required template part names are defined in `Themes/Generic.xaml`.

- To restyle, override the control templates and maintain the expected named parts (see control source for `Part` names used in `OnApplyTemplate`).
- NumberBox exposes formatting/mask options (via `MaskParser`) that you can use to control decimal places, signs, and padding.

## Accessibility

- Controls expose automation properties (name/value) and follow standard keyboard patterns:
  - Tab / Shift+Tab to move focus
  - Enter to commit edits
  - Esc to cancel
- When creating custom templates, ensure automation peers and focusable elements are preserved.

## Troubleshooting & tips

- If `Validate` handlers don't run, confirm the event is wired in XAML or in code-behind and bindings are correct.
- When updating multiple vector components at once, prefer `VectorBox.SetValues(...)` to avoid reentrancy and multiple validation cycles.
- Prefer using the CLR property setters (or `SetValue`) for programmatic DP updates; for multi-component updates use `VectorBox.SetValues(...)` to avoid multiple change notifications. The control internally uses an `isSyncingValues` reentrancy guard while synchronizing programmatic updates to prevent feedback loops.

## Try it (local development)

Open the solution in Visual Studio and build the `Controls.InPlaceEdit` project. The project uses standard .NET/MSBuild tooling.

> [!TIP]
> If you make template changes, reload any running demo apps to pick up the modified styles. The default templates live in `Themes/Generic.xaml`.

## Files of interest

- `src/VectorBox/` — VectorBox control and helpers
- `src/NumberBox/` — NumberBox implementation, mask parser, and input helpers
- `src/Label/` — InPlaceEditableLabel control and XAML
- `Themes/Generic.xaml` — default control templates and styles
