# NumberBox (WinUI 3)

A lightweight, stylable numeric input control for WinUI 3 (DroidNet.Controls.NumberBox).

This control combines a display mode and an edit mode, supports masked formatting, mouse-drag/wheel increments, visual states for validation, and an "indeterminate" presentation useful for view-model scenarios.

> [!note]
> This README documents the `NumberBox` control shipped in this repository. For full implementation details see the source files in this folder (e.g. `NumberBox.cs`, `MaskParser.cs`, `NumberBox.xaml`).

## Key features

- Formatted numeric display and editable TextBox.
- Mask-driven parsing/formatting (precision, units, sign, spacing).
- Visual states for valid/invalid and editing modes.
- Mouse drag and wheel support for changing value.
- Indeterminate display mode (preserves numeric backing value).
- Template parts exposed for full styling and templating.

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
    // e.Value contains the float to validate; set e.IsValid accordingly
    e.IsValid = e.Value >= 0 && e.Value <= 1000;
};
```

## Important properties (summary)

- `NumberValue` (float) — numeric backing value.
- `DisplayText` (string) — formatted text shown in display mode.
- `Label` (string) — label text.
- `LabelPosition` (enum) — None, Left, Top, Right, Bottom.
- `Multiplier` (int) — used for step adjustments.
- `Mask` (string) — formatting mask (see below).
- `WithPadding` (bool) — pad integer portion to mask width.
- `IsIndeterminate` (bool) — show `IndeterminateDisplayText` instead of formatted value.
- `IndeterminateDisplayText` (string) — text shown when `IsIndeterminate` is true (default `-.-`).
- `HorizontalValueAlignment` (TextAlignment) — alignment for the value text.
- `HorizontalLabelAlignment` (HorizontalAlignment) — alignment for the label.

## Events

- `Validate` — event with `ValidationEventArgs<float>` raised during validation. Handlers should set `e.IsValid` appropriately.

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

Note: a mask consisting only of `.` is invalid and will throw (see `MaskParser` constructor).

## Template parts & visual states

Control template parts (names used in `NumberBox.xaml`):

- `PartRootGrid` — `CustomGrid` (exposes an `InputCursor` property used while dragging).
- `PartBackgroundBorder` — `Border` around the content.
- `PartValueTextBlock` — `TextBlock` used for display mode.
- `PartLabelTextBlock` — `TextBlock` for the label.
- `PartEditBox` — `TextBox` used in edit mode.

Visual state groups & names (used by the control to animate UI):

- Group `CommonStates`: `Normal`, `Hover`, `Pressed`.
- Group `ValueStates`: `ShowingValidValue`, `ShowingInvalidValue`, `EditingValidValue`, `EditingInvalidValue`.

These states are defined in `NumberBox.xaml` and you can extend or replace the `DefaultNumberBoxStyle` to change appearance.

## Indeterminate mode and ViewModel guidance

The control preserves a numeric `NumberValue` internally even when `IsIndeterminate` is true. `IsIndeterminate` is a presentation flag only. If your view-model uses a nullable numeric value, map it to the control like this:

- ViewModel exposes `float? NullableValue`.
- When `NullableValue` is `null`, set `IsIndeterminate = true` and keep the last `NumberValue` if you want to preserve it.
- When `NullableValue` is not `null`, set `NumberValue` and `IsIndeterminate = false`.

This keeps UI presentation and underlying numeric state decoupled and predictable.

## Styling and templating

The folder contains `NumberBox.xaml` which declares `DefaultNumberBoxStyle`. To customize visuals, override the style in your application resources or create a new `ControlTemplate` that reuses the named parts and visual states listed above.

## Implementation notes

- Mouse and pointer dragging change the cursor while adjusting values; the control uses a private `CustomGrid` type to set the input cursor during drag operations.
- Validation is performed via the `Validate` event. The control sets internal validity state from the event.

## Building / running (repo)

From the repository root you can build the solution with dotnet (WinUI3 projects typically use Visual Studio; CLI build may require the proper SDK/workload installed):

```powershell
# from repo root (Windows PowerShell / pwsh)
dotnet build
```

If you prefer Visual Studio, open the solution and build/run the demo app that contains examples for the control.

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
