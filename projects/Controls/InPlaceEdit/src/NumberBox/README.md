# NumberBox (WinUI 3)

A lightweight, stylable numeric input control for WinUI 3 (DroidNet.Controls.NumberBox).

This control combines display and edit modes, supports masked formatting, horizontal scrubbing by dragging its label or its value, validation states, and an "indeterminate" presentation useful for view-model scenarios. Wheel input remains available to scroll the containing view.

> [!note]
> This README documents the `NumberBox` control shipped in this repository. For full implementation details see the source files in this folder (e.g. `NumberBox.cs`, `MaskParser.cs`, `NumberBox.xaml`).

## Key features

- Formatted numeric display and editable TextBox.
- Mask-driven parsing/formatting (precision, units, sign, spacing).
- Visual states for valid/invalid and editing modes.
- Horizontal scrubbing by dragging a visible label or the value; a click on the
  value opens text edit.
- Clicking outside the NumberBox ends text editing: valid input commits and invalid input is canceled.
- Optional compact labels fit to their text while the value editor fills the remaining width.
- Wheel input remains available to scroll the containing view and never edits the value.
- Optional label foreground for axis cues such as red X, green Y, and blue Z.
- Hover and drag emphasize the existing label color through opacity only; font
  metrics, wrapping, and editor geometry do not change.
- Indeterminate display mode (preserves numeric backing value).
- Template parts exposed for full styling and templating.
- Text editing follows the container's current width; entering edit mode does not
  lock the previously arranged width or force an adaptive parent into a new layout.

## Quick start

1. Add the `DroidNet.Controls` namespace to your XAML page (namespace mapping depends on your project):

```xml
xmlns:controls="using:DroidNet.Controls"
```

1. Use the control in XAML:

```xml
<controls:NumberBox
    NumberValue="123.45"
    Label="Amount"
    LabelPosition="Top"
    Multiplier="10"
    Mask="~.##"
    HorizontalValueAlignment="Center"
    HorizontalLabelAlignment="Left"
    WithPadding="False" />
```

1. Wire up validation or respond to value changes in code-behind or view-model:

```csharp
numberBox.Validate += (s, e) =>
{
    // e.NewValue contains the value to validate; set e.IsValid accordingly
    e.IsValid = e.NewValue >= 0 && e.NewValue <= 1000;
};
```

## Important properties (summary)

- `NumberValue` (double) — numeric backing value.
- `DisplayText` (string) — formatted text shown in display mode.
- `Label` (string) — label text.
- `LabelPosition` (enum) — None, Left, Top, Right, Bottom. Empty labels and `None` omit the label.
- `LabelForeground` (Brush) — optional label foreground override.
- `IsCompact` (bool) — when true with a non-empty `Left` or `Right` label, places the label inside the value field, before or after the value, and lets the value take the remaining width. `Top` and `Bottom` labels are never compact and ignore it.
- `Multiplier` (double, default `1`) — the change one DIP of horizontal drag makes; Shift makes a tenth of it. Use a fraction, such as `0.01`, for values with a small range.
- `Minimum`, `Maximum` (double, default unbounded) — the range of user input: a drag stops at a bound and typed text beyond one is invalid, like text the mask rejects. A value set by code beyond them is kept and shows as invalid.
- `Mask` (string) — formatting mask (see below).
- `WithPadding` (bool) — pad integer portion to mask width.
- `TrimTrailingZeros` (bool, default `false`) — omit fractional trailing zeros while retaining a leading zero, for example `0`, `45`, and `0.536`. Mask precision and units are preserved.
- `IsIndeterminate` (bool) — show `IndeterminateDisplayText` instead of formatted value.
- `IndeterminateDisplayText` (string) — text shown when `IsIndeterminate` is true (default `-.-`).
- `HorizontalValueAlignment` (TextAlignment) — alignment for the value text.
- `HorizontalLabelAlignment` (HorizontalAlignment) — alignment for the label.
- `AutoStackLabel` (bool) — moves an external horizontal label above the value when
  the available width cannot fit the label, the value field, and its annotations.
- `LabelWidth` (double) — shared external label-column width; NaN preserves the
  default proportional columns. Text scaling applies to a fixed label width.
- `LabelSpacing` / `LabelRowSpacing` (double) — horizontal and vertical label gaps.
- `Prefix` / `Unit` (string) — read-only annotations before and after the value
  border, such as `f/` or `m`, retained next to the input in both inline and stacked
  layouts. They are never edited.
- `UnitWidth` (read-only double) — the width the unit takes after the value border,
  including its gap, as of the last measure. Containers use it to line up the value
  fields of rows with different units.
- `ActualLabelPosition` (read-only) — placement selected during measurement.

## Events

- `Validate` — event with `ValidationEventArgs<double>` raised during validation. Handlers should set `e.IsValid` appropriately.

## Mask syntax and examples

`NumberBox` uses a compact mask format parsed by `MaskParser`.

Pattern (informal):

```text
[sign]? [beforeDecimal(~ or #...)] . [afterDecimal(#...)] [space]? [unit]?
```

- sign: optional `-` or `+` to require negative or positive values.
- beforeDecimal: `~` for unbounded integer part, or `#` repeated to indicate maximum integer digits.
- afterDecimal: `#` repeated to indicate number of fraction digits (precision).
- optional space: include a space before a unit if present.
- unit: trailing non-whitespace string appended to formatted output (e.g. `px`, `%`).

Examples:

- `~.##` — unlimited digits before decimal, two fractional digits (e.g. `12345.67`).
- `#.###` — up to 1 digit before decimal, three fractional digits (max value 9.999).
- `+.##` — positive-only value with two decimal places.
- `#.## %` — one digit before decimal, two decimals, and a trailing percent sign with a space: `1.23 %`.

A unit in the mask is part of the formatted value text: it shows inside the value
border and in the text editor. The `Unit` property is a separate annotation outside
the border that the user never edits. Use `Unit` for a property's unit; use a mask
unit only when the unit belongs to the value text itself.

Note: a mask consisting only of `.` is invalid and will throw (see `MaskParser` constructor).

## Template parts & visual states

Control template parts (names used in `NumberBox.xaml`):

- `PartRootGrid` — `CustomGrid` (exposes an `InputCursor` property used while dragging).
- `PartBackgroundBorder` — `Border` around the content.
- `PartValueGroup` — annotation/value `Grid`, positioned as a single group beside
  or below the native drag label.
- `PartValuePrefix` / `PartValueUnit` — read-only annotation `TextBlock`s.
- `PartValueTextBlock` — `TextBlock` used for display mode.
- `PartLabelTextBlock` — `TextBlock` for the label.
- `PartCompactLabelTextBlock` — `TextBlock` for the optional in-field label.
- `PartEditBox` — `TextBox` used in edit mode.

The standard template overlays the display and text editor in
`NumberBoxValuePanel`. The display value determines the measured footprint, while
the editor follows the arranged bounds, so native text-editor chrome and draft
expressions do not change the parent property's responsive layout.

Visual state groups & names (used by the control to animate UI):

- Group `CommonStates`: `Normal`, `Hover`, `Pressed`.
- Group `ValueStates`: `ShowingValidValue`, `ShowingInvalidValue`, `EditingValidValue`, `EditingInvalidValue`.

These states are defined in `NumberBox.xaml` and you can extend or replace the `DefaultNumberBoxStyle` to change appearance.

## Indeterminate mode and ViewModel guidance

The control preserves a numeric `NumberValue` internally even when `IsIndeterminate` is true. `IsIndeterminate` is a presentation flag only. If your view-model uses a nullable numeric value, map it to the control like this:

- ViewModel exposes `double? NullableValue`.
- When `NullableValue` is `null`, set `IsIndeterminate = true` and keep the last `NumberValue` if you want to preserve it.
- When `NullableValue` is not `null`, set `NumberValue` and `IsIndeterminate = false`.

This keeps UI presentation and underlying numeric state decoupled and predictable.

## Styling and templating

The folder contains `NumberBox.xaml` which declares `DefaultNumberBoxStyle`. To customize visuals, override the style in your application resources or create a new `ControlTemplate` that reuses the named parts and visual states listed above.

Prefer a style based on `DefaultNumberBoxStyle` for ordinary sizing and border
changes: its template respects `Padding`, `MinHeight`, `FontSize`,
`BorderBrush`, `BorderThickness`, and `CornerRadius`. `VectorBox.ComponentStyle`
applies the same NumberBox style to all channels without replacing templates.

## Implementation notes

- Dragging a visible label scrubs from the press. Dragging the value scrubs once the
  pointer has moved 4 DIPs sideways, and the click that ends such a drag does not open
  the text editor. A scrub steps once per DIP of horizontal travel; vertical movement
  and the rate of pointer events add nothing. `ScrubGesture` holds these decisions.
- The control uses a private `CustomGrid` type to set the input cursor while scrubbing.
- Validation is performed via the `Validate` event. The control sets internal validity state from the event.

## Building / running (repo)

Build with 64-bit Visual Studio MSBuild as described in
[Build workflows](../../../../../tooling/doc/build.md), and run the Controls demo app
for examples of the control.

## Troubleshooting

> [!tip]
> If formatting looks wrong, check the `Mask` value and `WithPadding` setting. Use `MaskParser` tests/examples to verify formatting behavior.

## Where to look in the source

- `NumberBox.cs` — core behavior, input handling, states.
- `MaskParser.cs` — mask parsing and formatting logic.
- `NumberBox.xaml` — default style and visual states.
- `CustomGrid.cs` — small helper exposing a protected cursor property for the grid.

## Final notes

This control is designed for integration into WinUI 3 apps that need a compact, predictable numeric input. It is intentionally minimal and relies on template parts and events for flexibility. If you need extensions (e.g. culture-aware formatting, localization, or additional keyboard behaviors) consider adding them in small, focused PRs.
