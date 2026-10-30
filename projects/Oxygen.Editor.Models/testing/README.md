# Oxygen.Editor.Models.Testing

The test kit of `Oxygen.Editor.Models`: the one writer of model import records
in the editor's tests. Only test projects reference it.

- `ModelRecords.Create(project, identity, source, settings, sourceIdentity)`
  makes the record of a model without writing it. `identity` is the model's
  virtual path (`/Content/Vehicles/Car.glb`), `source` is the source file's
  mount path; the settings are empty and the source identity new when omitted.
- `ModelRecords.Write(project, identity, source, ...)` creates the record and
  writes it where the contract places it, creating its folders;
  `Write(project, record)` writes a record a test changed.
- `PathOf(project, identity)` is the file path of a model's record, for a test
  that damages, moves or reads it.
- `SpellSource(identity, source)` is the text a record writes to name its
  source.

A test names the model and its source; where the record lives and how it
spells its source are the contract's, decided here. Tests whose subject is the
record's own rules (`ModelImportSettings.Create` and `Parse` refusals, the
place `PathFor` gives) call the production API, as the contract tests of
`ModelImportSettings`.

A change of the record contract is made here once, not in each test.
