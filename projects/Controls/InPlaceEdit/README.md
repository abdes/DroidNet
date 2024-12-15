# DroidNet In-Place Editable Controls

## In-Place Editable Label

A custom control backed by a value, shown as a `TextBlock`, which can be edited
in-place when double-clicked. The `TextBox` for editing the value can replace
the label while editing, or can be shown as an overloay flyout.

The control supports validation via a `Validate` event, fired whenever the value
changes, partially on each keystroke, or fully. The control shows a visual
indication when validation fails, and will not the change the backing value
unless validation succeeds.

## Number Box

A custom control backed by a `float` value, displayed as a text block with an
optional unit.

The value can be changed using mouse motion and scroll button, or can be edited
in-place with an overlay `TextBox`. When using mouse motion or scroll wheel to
change the value, the value is changed in increments, which can be set as a
property for the control.

The control supports validation via a `Validate` event, fired whenever the value
changes, partially on each keystroke, or fully. When using mouse motion or
scroll wheel, the control will not change the value unless the change is valid.
When using in-place editing via the flyout `TextBox`, it will show a visual
indication when validation fails, and will not the change the backing value
unless validation succeeds.

The control can have an optional header label, which can be positioned to the
left, right, bottom or top of the value.

## Vector Box

A custom control which can edit multiple numeric values in a row or a column.
For each backing value, the control will use aa `NumberBox`.
