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
  Native compatibility and integrity verification remain active.
- `../testsupport/Filesystem` owns shared publication and generation fixtures.
  Every scenario owns its mutable project, files and leases.
- `../testsupport/WorkerProbe` is the real child-process fixture. Only Integration
  builds and stages it. Cancellation and termination tests operate on their own
  child processes.

Use explicit completion signals for asynchronous work. Do not add screenshots,
archives or attachments merely to record execution. Retain hashes that exercise
source freshness or content integrity, and measurements useful for comparison.
