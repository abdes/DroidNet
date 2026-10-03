# Inspector Numeric Editing

## Transform Input

- A number or arithmetic expression such as `90/2` assigns one absolute value to every selected target.
- `+=2` and `-=2` apply an offset to each target's value captured when text editing began. `*2`, `/2`, `*=2` and `/=2` multiply or divide each target's captured value.
- Relative text remains a draft until commit. Enter commits the selected targets as one Transform history entry; Escape changes neither the scene nor history.
- The edit is all-or-nothing. Non-finite results, division by zero, and values rejected by any selected target's Transform validation are not applied.

## Ownership

`NumberBox` parses arithmetic and carries the raw text through validation and commit events so `TransformViewModel` can distinguish relative operations. The Transform view model reads and writes values through the canonical Transform descriptors and submits target-specific edits through `EditPropertiesForTargetsAsync`; the scene command service owns validation, synchronization, dirty state and history.

## Numeric Control Presentation

- Labels are optional. An empty label or `LabelPosition=None` consumes no label space.
- Compact left labels sit inside the field border; the label uses its measured content width and the value editor fills the remaining width.
- Horizontal scrubbing starts on a visible label, never on the value region. Clicking the value enters text editing.
- A pointer press outside the NumberBox ends text editing even when the target cannot take focus. Valid drafts commit; invalid drafts cancel.
- Wheel input is left to the containing scroll view. Vector labels may override text (`X/Y/Z` or `R/G/B`) and foreground brushes independently.

## Responsive Property Rows

`Oxygen.Editor.Controls.PropertyCard` owns `Layout=Auto/Inline/Stacked`.
Auto measures the available card width, not the window: a shared 124-DIP
`LabelWidth`, a 12-DIP gap, and `EditorMinimumWidth` (128 for scalars, 248 for
XYZ), plus the measured prefix, qualifier and leading accessory footprint.
System text scaling and larger measured editors increase the required width.
Labels wrap rather than losing essential words to ellipsis.

Scalar Inspector cards opt into `PropertyCard.UseEditorLabel`: the owning card
projects its caption, layout policy and annotations onto a single native
NumberBox, including a NumberBox followed by diagnostics. NumberBox owns the
actual drag label and responsive value region; no duplicate static caption or
forwarded pointer handler is used. Replacing content or disabling composition
restores the editor's original local values and bindings. Vector and multi-input
compositions retain their own labels and the card header.

`Qualifier`, `Prefix`, and `LeadingContent` are shared presenters, not numeric
text. Scalar qualifiers stay beside the input in both layouts. Compound
qualifiers follow the complete inline vector or move to the right of the
stacked header. RGB colors and dimensionless RGB multipliers explicitly use
Stacked; colors supply a swatch through LeadingContent, multipliers do not.
An empty prefix, accessory or qualifier consumes no column gap.

Inspector vectors opt into `VectorBox.AutoStackComponents`. When full-width
channels still cannot preserve their usable widths, they become vertical
without replacing editors, changing channel order, or ending transactions.
The color swatch remains at the top of the value region. Each channel's
automation name identifies its property, channel and shared annotation.

`PropertiesExpander.HeaderActions` hosts independent reset actions before the
native disclosure. The native header, icon, wrapping description, expansion
state and keyboard semantics remain intact. Shared Inspector styles use the
standard control templates; view-local NumberBox, VectorBox and PropertyCard
templates are not needed. Numeric fields target a 32-DIP compact height using
minimum heights and padding, so enlarged text can grow them.

Nested disclosure headers stay neutral when expanded. Chevron direction conveys
expansion; hover and pressed feedback remain subtle, and keyboard focus retains
the native focus visual. Checked-state resources are scoped to disclosure
headers, not selection toggles elsewhere in the Inspector.

## Cursor Feedback

Keep the horizontal scrub cursor change tied to a pressed/active drag, not hover alone. Microsoft recommends visual hover feedback instead of using a cursor as hover feedback; use a label hover treatment to teach the scrub affordance. The drag handle is the visible label, so `SizeWestEast` is shown once the drag begins.

Native labels emphasize their existing foreground through opacity only, from
0.85 normally to 1 during hover or drag. Font weight, size and geometry remain
unchanged, and compact channel labels retain their RGB/axis brushes.

## Background Color

The Background card uses a Color / Linear RGB header above one compact row:
a display-converted swatch and three equally sized, bordered numeric fields with
colored R/G/B labels. Numeric values remain linear; the swatch picker converts
between display sRGB and linear authoring values. Reset restores the scene
model's default black background through the normal scene command path as one
undoable edit, without changing atmosphere or post-processing.

Directional-light color and Ground Albedo use the same sRGB picker conversion
and linear numeric channels. Display conversion never rewrites an existing
authored color; only a picker edit submits a converted value through the
existing transaction coordinator. RGB multipliers remain unconverted numeric
values.

## Model and View Boundary

Environment and Directional Light property models expose linear `Vector3`
colors and accept linear authoring edits. Views convert picker sRGB values
before submitting edits and create display colors and swatch brushes through
`InspectorRgbPresentation`. Exposure-curve preview points are projected from
authored keys by the view's `InspectorCurvePresentation`, not the model.

Shared field diagnostics expose `HasError`; views convert that boolean to
visibility with the standard converter. Models do not construct UI objects
to report validation errors.

Environment asset notifications use an injected reactive `IScheduler`.
Inspector composition supplies the existing hosting dispatcher scheduler;
standalone models use immediate delivery without acquiring a WinUI dispatcher.
Disposing the model cancels queued asset notifications and removes the feed
subscription.

## Verification

Focused Debug/x64 verification passes 117 Inspector UI cases, 75 InPlaceEdit
control/parser cases, and 40 native component-field cases covering editing,
undo/redo, Save/reopen and engine synchronization. The runnable editor builds
with MSBuild. Native captures were reviewed for compact Background layouts,
inline XYZ, enlarged-text stacked channels, and neutral expanded disclosure
headers in Light and Dark themes.

Headless verification passes 90 color/history, scheduling, diagnostics and
scene-command cases without initializing WinUI, including all six
`ColorHistoryTests` cases. Native Inspector coverage includes real Background,
Ground Albedo and Directional Light picker events, plus exposure-curve updates
and undo.
Native pointer coverage exercises scalar caption dragging, one-step history,
undo/redo and Escape cancellation for Camera, Directional Light and Environment;
shared scalar rows retain the same drag label through responsive reflow and can
drag again after unloading/reloading. Light/Dark disclosures repeatedly expand
and collapse with the correct glyph. Hover/pressed regressions verify unchanged
font metrics and geometry for external and compact labels at enlarged text sizes.
Complete workspace visual acceptance and a native Comfortable-density selector
are outside this focused evidence.
