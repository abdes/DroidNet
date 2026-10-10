# Cooker tests

How the Cooker tests are organized, how to run them, and where a new test
goes. The full rationale is in
[the test suite plan](../../../../design/content-pipeline/cooker-test-suite-plan.md).

## Run

| What                     | Command                                                                  |
| ------------------------ | ------------------------------------------------------------------------ |
| Fast loop (unit)         | `ctest --test-dir <build> -C Debug -R "^Oxygen\.Cooker\." -L unit`       |
| Before a PR (functional) | `ctest --test-dir <build> -C Debug -R "^Oxygen\.Cooker\." -L functional` |
| Everything Cooker        | `ctest --test-dir <build> -C Debug -R "^Oxygen\.Cooker\."`               |
| One executable, filtered | `Oxygen.Cooker.Import.Unit.Tests --gtest_filter=TextureCooker*`          |

## Tiers

Each executable holds tests of one tier. The tier is a CTest label.

- **unit**: in memory, single thread, no file writes, no model assets. Reading
  a checked-in JSON schema through `SchemaPath()` is allowed.
- **functional**: anything that writes files, reads model assets, starts
  threads (thread pool, `AsyncImportService`, `AsyncImporter`), starts a
  subprocess, or runs several components together.
- **gpu**: needs a real graphics device. None exist today; register them with
  the `GPU` tier, which also takes the `oxygen_gpu` resource lock.

When in doubt, choose the slower tier.

## Where is the test for X?

Source `Cooker/<Dir>/<Stem>.{h,cpp}` is tested in
`Test/<Dir without Internal/>/<Stem>[_<aspect>][_functional|_gpu]_test.cpp`:

| Source                                                                 | Tests                            |
| ---------------------------------------------------------------------- | -------------------------------- |
| `Import/`, `Import/Internal/`, `Import/Internal/{Utils,bc7,fbx,gltf}/` | `Test/Import/`                   |
| `Import/Internal/Jobs/`                                                | `Test/Import/Jobs/`              |
| `Import/Internal/Pipelines/`                                           | `Test/Import/Pipelines/`         |
| `Import/Internal/Emitters/`                                            | `Test/Import/Emitters/`          |
| `Import/Schemas/*.schema.json`                                         | `Test/Import/Schemas/`           |
| `Loose/`                                                               | `Test/Loose/`                    |
| `Pak/`, `Pak/Schemas/`                                                 | `Test/Pak/`, `Test/Pak/Schemas/` |
| `Tools/<Tool>/`                                                        | `Tools/<Tool>/Test/`             |
| Workflows across Cooker areas (import → loose → pak)                   | `Test/Scenarios/`                |

Every test file names the production files it covers on a `// Covers:` line
after the license block. To find the tests for a source file:

```sh
rg "Covers:.*<Stem>" Test Tools
```

## Executables

| Executable (`Oxygen.Cooker.` …)     | Tier       | Directory                           |
| ----------------------------------- | ---------- | ----------------------------------- |
| `Import.Unit.Tests`                 | unit       | `Test/Import/**` unit files         |
| `Import.Functional.Tests`           | functional | `Test/Import/*_functional_test.cpp` |
| `Import.Jobs.Functional.Tests`      | functional | `Test/Import/Jobs/`                 |
| `Import.Pipelines.Functional.Tests` | functional | `Test/Import/Pipelines/`            |
| `Import.Emitters.Functional.Tests`  | functional | `Test/Import/Emitters/`             |
| `Loose.Unit.Tests`                  | unit       | `Test/Loose/`                       |
| `Loose.Functional.Tests`            | functional | `Test/Loose/`                       |
| `Pak.Unit.Tests`                    | unit       | `Test/Pak/`, `Test/Pak/Schemas/`    |
| `Pak.Functional.Tests`              | functional | `Test/Pak/`                         |
| `Scenarios.Functional.Tests`        | functional | `Test/Scenarios/`                   |
| `Support.Functional.Tests`          | functional | `Test/Support/`                     |
| `PakTool.Unit.Tests`                | unit       | `Tools/PakTool/Test/`               |
| `PakTool.Functional.Tests`          | functional | `Tools/PakTool/Test/`               |
| `Inspector.Functional.Tests`        | functional | `Tools/Inspector/Test/`             |
| `ImportTool.Functional.Tests`       | functional | `Tools/ImportTool/Test/`            |

The ImportTool CLI is also tested by two Python tests,
`Oxygen.Cooker.ImportTool.SourceAnalysis.Cli` and
`Oxygen.Cooker.ImportTool.RetainedImport.Cli`.

## Adding a test

1. Put it in the file the mapping rule gives. Create the file if it does not
   exist, with its `// Covers:` line.
2. Add the file to the matching `oxygen_cooker_test(<Area> <TIER> …)` call.
   Keep source lists alphabetical, one file per line.
3. Use the helpers in `Test/Support/` instead of writing new ones:
   `TempDirTest`/`ScopedTempDir`, `ModelPath`, `SchemaPath`, `LoadSchema`,
   `ValidateJson`, `ReadBytes`/`WriteText`, `HasDiagnosticCode`,
   `DiagnosticSummary`.
4. A Cooker test checks Cooker output. Read it back through `lc::Inspection`,
   `lc::ValidateRoot`, Data format types, `content::PakFile` or parse-only
   loaders. Never use `content::internal` or construct an `AssetLoader`;
   Content runtime behaviour is tested in Content.
5. One behaviour per test. Use `ASSERT_*` before dereferencing anything. No
   sleeps, and always bound waits. Write only inside the test's temp
   directory.
