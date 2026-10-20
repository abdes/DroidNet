# Oxygen.Editor.Input

The input family module: what is true of the editor's input sources whatever
shows them. It holds no view, view model or dialog.

- `InputContainer` is the model over the format `InputSource`
  (`Oxygen.Managed.Assets`): an input source (`Player.input.json`) and the
  actions (`InputAction`) and mapping contexts (`InputMappingContext`) it
  declares, each identified under the source
  (`/Content/Input/Player.input/Jump.oiact`).
- `InputRules` checks what needs the other sources of the project: an action
  named by virtual path must be declared by the source it is named under. The
  format checks the rest (JSON, the engine's schema, duplicates, actions named
  by name).
- `StartingContextRules` serves a scene's starting input contexts, in the
  inspector's Play section and in the cook: whether a context is the project's,
  the priority its source declares, the priority it starts with, and the
  problems that stop it cooking (`StartingContextCodes`).
- `IInputSources`, implemented by `InputFacts`, holds the saved input sources of
  the active project and their problems (`InputProblem`). It reads a source
  again when its file changes and checks every source again, since one can name
  the actions of another. Its events are raised on each subscriber's context.
- `InputSourceFiles` finds a source's file from its identity and reads it as
  the cooker will; the cook reads the saved file through it.
- `InputTemplates.New` is the text of a new input source, and
  `InputTemplates.Asset` the template New Input File creates it from.

Files are read through `IFileSystem`, so the tests run on an in-memory file
system. `Oxygen.Editor.Input.Testing` gives other modules' tests
`FakeInputSources`.

The Input Editor's document kind comes with that editor.
