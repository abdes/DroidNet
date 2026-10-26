# Oxygen.Editor.Controls

The editor's shared WinUI control library: property rows, the sections that
hold them and the field editors every editor module composes, with one look
owned here. Namespace `Oxygen.Editor.Controls`.

## Controls

| Control              | Role                                                                                    |
| -------------------- | --------------------------------------------------------------------------------------- |
| `PropertyCard`       | One property row: label, editor, qualifier, override bar and reset; inline or stacked.  |
| `PropertiesExpander` | A collapsible section of rows with a header, icon, description and header actions.      |
| `NumberField`        | A row around a NumberBox: unit, prefix, mixed value, validation and applicability text. |
| `ToggleField`        | A row around a ToggleSwitch; a `null` value shows mixed.                                |
| `ColorField`         | A row around a linear ColorBox; `ColorFieldKind` decides range, picker and qualifier.   |
| `SourceModeField`    | A row of `Segmented` modes with a caption line and an optional source button.           |
| `DiagnosticCard`     | A finding: message, a preview of the items it is about, expandable to all; no icon.     |

`OverrideState`, `OverrideStates` and `PropertyLayout` are the rows' shared
types. The fields never edit a model: they expose values and forward their
editors' events with themselves as sender, so a field's `Tag` names it.
`DiagnosticCard` takes its background from `DiagnosticCardSeverity`; its
items show in place, never only in a tooltip.

## Styles

All controls are templated. Their default styles live in `src/PropertyEditors/`,
one dictionary per control, and `src/Themes/Generic.xaml` merges them. Each
control dictionary merges the shared resources it uses (the brushes and metrics
of `src/Themes/ThemeResources.xaml`, the value box looks of
`src/PropertyEditors/PropertyValueBoxes.xaml`), so it resolves them wherever it
is merged. The framework loads `Generic.xaml` for the controls'
`DefaultStyleKey`; consumers
never merge it, and never redefine its resource keys.

Consumers merge only `src/Themes/EditorStyles.xaml`, into `Application.Resources`
after `XamlControlsResources`, for the keyed styles of their own elements:

```xml
<ResourceDictionary Source="ms-appx:///Oxygen.Editor.Controls/Themes/EditorStyles.xaml" />
```

| Key                             | For                                                     |
| ------------------------------- | ------------------------------------------------------- |
| `PropertyNumberBoxStyle`        | A standalone `DroidNet.Controls.NumberBox` in a row.    |
| `PropertyVectorBoxStyle`        | A standalone `DroidNet.Controls.VectorBox` in a row.    |
| `QuietActionButtonStyle`        | A borderless icon button, such as add, remove or reset. |
| `UnitSuffixTextStyle`           | A unit after a value.                                   |
| `RightAlignedToggleSwitchStyle` | A switch at the end of a settings row.                  |

## Tests

`tests/Unit.UI` hosts the controls in the shared WinUI test host.
`testing/` is the test kit (`PropertyEditorProbes`) other modules' UI tests use
to find and drive the controls' template parts.
