# ContentPipeline tests

Choose an [execution tier](#execution-tiers), find the [owning feature](#ownership),
and follow the [fixture rules](#fixtures).

## Execution tiers

| Project suffix       | Purpose                                                                                                    | Dependencies                                                                  |
| -------------------- | ---------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| `.Unit.Tests`        | Orchestration, descriptors, manifests, snapshots, status and controlled worker contracts                   | Managed code and small temporary fixtures; no installed cooker                |
| `.Integration.Tests` | Native import/cooking, dependency interpretation, publication/recovery, mounting and real worker lifetimes | Matching installed SDK for native cases; Windows filesystem/process semantics |
| `.Benchmarks.Tests`  | Large shared-source status workload and elapsed-time measurement                                           | Integration prerequisites                                                     |

These programs use ordinary .NET hosts. None requires a WinUI window or desktop
input. `open.cmd` includes all three projects by default:

```powershell
./projects/Oxygen.Editor.ContentPipeline/open.cmd

# Optional fast-only solution alongside the complete solution.
./tooling/GenerateSolution.ps1 -Scope projects/Oxygen.Editor.ContentPipeline -TestScope Unit -SolutionPath artifacts/ContentPipeline.Unit.sln -Launch
```

Build in Visual Studio or with Visual Studio MSBuild. Then run selected tests in
Test Explorer, or use the repository command without rebuilding:

```powershell
traverse Invoke-Tests --start projects/Oxygen.Editor.ContentPipeline/tests/Unit -- --filter FullyQualifiedName~AssetCookStatusTests
traverse Invoke-Tests --start projects/Oxygen.Editor.ContentPipeline/tests/Integration -- --filter FullyQualifiedName~PublisherTerminationTests
traverse Invoke-Tests --start projects/Oxygen.Editor.ContentPipeline/tests/Benchmarks
```

Selecting the entire `tests` directory includes every program below it.

Integration runs at most two test classes concurrently; cases within each class
remain sequential. Process-wide exception probes opt out with `DoNotParallelize`.
Benchmarks remain serialized. This uses MSTest's supported
[class-level execution policy](https://learn.microsoft.com/en-us/dotnet/core/testing/unit-testing-mstest-writing-tests-controlling-execution).

## Ownership

Folders and namespaces follow Cooking, Descriptors, Import, Inspection,
Snapshots, Status, Processes, Publication and Mounting. Classes identify a
specific responsibility: queue priority, source replacement, dependency discovery,
incremental cooking, repair or publication recovery. They do not share a
module-wide partial test class.

Publication tests retain real journals, handles and crash boundaries. Native
interpretation remains covered with the installed tools. The three-output
shared-source integration case checks correctness; the benchmark exercises the
same assertions with 256 outputs.

## Fixtures

- Keep helpers with a single test-class consumer beside that class.
- `../testsupport/Controlled` owns reusable managed fixtures and test doubles.
  Source-analysis callbacks are explicit. Dependency-free orchestration tests
  use controlled material/scene facts; status tests declare their dependency
  graph directly. Neither fixture parses native descriptors or starts a cooker.
- `../testsupport/Native` composes actual native services and shared workflows.
  Ordinary scenarios use the production cooking compatibility service instead
  of constructing and then verifying a redundant SDK receipt. Producer-mutation
  scenarios keep their controlled receipts. Native compatibility, protected
  artifact leases and content-integrity verification remain active.
- `../testsupport/Filesystem` owns shared publication and generation fixtures.
  Every scenario owns its mutable project, files and leases.
- `../testsupport/WorkerProbe` is the real child-process fixture. Only Integration
  builds and stages it. Cancellation and termination tests operate on their own
  child processes.

Keep imported projects and publication/recovery state private to each case.
Their provenance includes project identities and absolute source paths; copying
a cooked project is not a safe fixture shortcut. Only portable, immutable cooked
library inputs are candidates for shared preparation.

Use explicit completion signals for asynchronous work. Do not add screenshots,
archives or attachments merely to record execution. Retain hashes that exercise
source freshness or content integrity, and measurements useful for comparison.

## Focused performance baseline

Local Debug comparison on 2026-10-01, AMD Ryzen 9 9950X, .NET 9. The same
18 transitive-library and import-replacement cases passed in each run. These
are elapsed times for that subset, not a full-suite performance claim.

| Configuration                                       | Elapsed |
| --------------------------------------------------- | ------: |
| Receipt fixture, serial                             |  49.5 s |
| Production cooking compatibility, serial            |  48.1 s |
| Production cooking compatibility, two class workers |  25.8 s |

```powershell
dotnet test --project projects/Oxygen.Editor.ContentPipeline/tests/Integration/Oxygen.Editor.ContentPipeline.Integration.Tests.csproj --no-build -c Debug --filter 'FullyQualifiedName~TransitiveLibraryTests|FullyQualifiedName~ImportReplacementTests' --minimum-expected-tests 18
```
