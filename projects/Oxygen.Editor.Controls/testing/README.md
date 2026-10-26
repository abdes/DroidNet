# Oxygen.Editor.Controls.Testing

The test kit of `Oxygen.Editor.Controls`: what UI tests use to find and drive
the parts of the library's property editors, instead of naming template parts
themselves. Only test projects reference it.

- `PropertyEditorProbes.CardOf(field)` is a field's row `PropertyCard`, from its
  template.
- `SwatchOf(colorField)` and `SwatchColor(colorField)` are a `ColorField`'s
  swatch button and the color it shows; `PickDisplayColorAsync(swatch, color)`
  picks a color through the swatch's picker and commits it by closing it.
- `EnterTextAsync(numberBox, text)` begins a text edit on a NumberBox, such as a
  `NumberField`'s, and types the text; the caller commits it.

A change to a template part is made here once, not in each test.
