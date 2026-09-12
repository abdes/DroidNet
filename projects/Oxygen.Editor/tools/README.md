# Editor artifact qualification

Build the editor, Managed.Core tests, ContentPipeline tests, Runtime tests and
WorldEditor UI tests in the chosen configuration. The engine SDK must be
installed in that configuration. Commit the source before qualification.

Unlock Windows and keep the UI test window in the foreground for pointer tests.
Run the explicit qualification command in PowerShell on .NET 9 or later:

```powershell
./projects/Oxygen.Editor/tools/Qualify-EditorArtifacts.ps1 -Configuration Debug
./projects/Oxygen.Editor/tools/Qualify-EditorArtifacts.ps1 -Configuration Release
```

The command holds read leases on the candidate artifacts and matching test
assemblies while all four suites run. Every test must pass. Editor and installed
engine base schemas must match. Source or inventory changes stop promotion.

Successful qualification writes `qualification/<Configuration>.json` beside the
editor, plus a report named with the manifest hash. The report records the test
results, exact test assembly hashes, source revision and machine information.
Failure preserves the previously accepted manifest. Ordinary builds do not
refresh qualification; changed artifacts must be qualified explicitly again.

Use `-EditorRoot`, `-EngineRoot` or `-VSTestPath` for explicit installation/tool
locations. A packaged installation places its engine SDK in `Engine` beside the
editor; development uses the checkout's `Oxygen.Engine/out/install/<Configuration>`.
