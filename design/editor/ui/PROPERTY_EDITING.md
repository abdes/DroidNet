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

## Cursor Feedback

Keep the horizontal scrub cursor change tied to a pressed/active drag, not hover alone. Microsoft recommends visual hover feedback instead of using a cursor as hover feedback; use a label hover treatment to teach the scrub affordance. The drag handle is the visible label, so `SizeWestEast` is shown once the drag begins.

## Verification

Parser and UI tests cover expression evaluation, per-target offsets, undo/redo and Escape cancellation. Build, test execution and editor UI review are pending user verification.
