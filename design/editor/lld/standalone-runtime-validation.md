# Standalone Runtime Validation LLD

Status: `V0.1 design contract; ED-M08 implementation and validation pending`

## 1. Purpose

Prove that the exact saved/published Oxygen Editor project scene loads through
native runtime content APIs and renders the same authored content as embedded
preview. Validation is read-only for authored and published files.

## 2. PRD Traceability

`GOAL-001`, `GOAL-003`, `REQ-018`, `REQ-019`, `REQ-022` through `REQ-026`,
`REQ-030`, `REQ-037`, `REQ-039` through `REQ-042`; `SUCCESS-001`,
`SUCCESS-003`, `SUCCESS-004`, `SUCCESS-006`.

## 3. Architecture Links

- [runtime-integration.md](./runtime-integration.md): scene/view completion,
  qualification, published-root leases, and capture boundary.
- [content-pipeline.md](./content-pipeline.md): immutable input snapshot,
  staged validation, publication metadata and output transaction.
- [environment-authoring.md](./environment-authoring.md): authored field semantics.
- [property-pipeline.md](./property-pipeline.md): current-revision live projection.
- [PRD.md](../PRD.md) sections 8-10: scope, workload, and release acceptance.

## 4. Source Baseline

RenderScene already uses the engine AssetLoader and scene instantiation path.
Its existing `--scene` startup override searches checked-in example content and
accepts fuzzy names. That is not a sufficient editor-project validation entry
point. The exact request, observations, and capture/report contract below must
be implemented and validated in ED-M08; this LLD does not claim those options
or artifacts exist today.

## 5. Target Workflow

1. Save participating documents explicitly and publish a successful cook.
2. Select `Validate in Standalone` for the active scene from its existing scene
   command/menu surface. The managed workflow rejects dirty dependencies,
   uncommitted publication journals, missing output, and build/schema mismatch.
3. Acquire a read lease on the published project output. Capture expected state
   from the saved authoring/descriptor snapshot associated with that publication,
   plus its URI-to-cooked-node/asset mapping. Do not derive expectations from
   whatever the live runtime happens to contain.
4. Acquire the bounded embedded capture session defined below. Synchronize the
   saved revision, pin the validation camera/profile, disable editor overlays,
   and capture observed state/image while newer authoring sync remains pending.
   Release that session and converge to current authoring/view state afterward;
   never save the temporary camera/profile as authoring data.
5. Launch the matched RenderScene executable with an exact validation request.
   It mounts only the requested roots, loads exactly the requested scene, applies
   the controlled profile, and writes native observed-state and capture artifacts.
6. Compare both observations to expected authored semantics and compare the
   controlled images. Publish a structured result with artifact paths.
7. Release the output lease after the child process exits and all reads finish.
   New edits during validation remain dirty and mark the result as applying to
   the older saved revision; they cannot be cleared by validation completion.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| WorldEditor scene command | User entry point and active-document context. |
| ContentPipeline managed validation coordinator | Preflight, expected-state/request generation, output lease, matched tool discovery, process lifetime, comparison and result publication. |
| Runtime | Embedded observed state/capture via stable managed engine capabilities; view restoration. |
| RenderScene / reusable DemoShell loader code | Exact-root native loading, native observations, controlled rendering, process result. |
| Engine | Asset/scene interpretation, rendering and supported capture APIs; editor code never reads raw PAK offsets. |

No new standalone authoring domain or generic validation dashboard is introduced.
The native validation entry point is implemented in RenderScene, not an editor
copy of runtime loading/rendering.

## 7. Request And Result Contracts

ED-M08 adds the required CLI entry point:

```text
Oxygen.Examples.RenderScene.exe --editor-validation-request <absolute-request.json>
```

This is a target CLI contract, not an existing supported invocation. The request
is versioned JSON with these required fields:

| Field | Meaning |
| --- | --- |
| `request_version` | Exactly 1; other versions fail before loading. |
| `operation_id`, `project_id` | Stable correlation and project identity. |
| `publication_id` | Successfully committed cook operation identity. |
| `build_fingerprint` | Editor/native/tool/schema hashes and build configuration. |
| `roots` | Ordered absolute cooked-root paths, mount names, index hashes; exact allowlist. |
| `scene_virtual_path`, `scene_asset_key` | Exact scene identity; both must resolve to the same cooked asset. No substring/stem fallback. |
| `expected_state_path`, `expected_state_hash` | Saved authoring/descriptor expectation and identity mapping. |
| `profile` | Camera identity, resolution, fixed timestep, seed, exposure/render settings and warm-up/capture frames. |
| `artifact_directory` | Operation-owned output directory outside authored content. |

Expected state contains node hierarchy and source-to-cooked identity mapping,
transforms, geometry/material resolutions, all editable camera/light fields,
material values, and environment/post-process values. Native observed state
comes from loaded scene/assets and effective scene systems, not by echoing the
request or reading editor JSON as runtime truth.

Artifacts: `request.json`, `expected-state.json`, `embedded-observed.json`,
`standalone-observed.json`, `embedded.png`, `standalone.png`, `comparison.json`,
`result.json`, and correlated logs under `.oxygen/validation/<OperationId>`.
Results identify phase, status, diagnostic codes, mismatch JSON pointers,
maximum errors, image metrics, captured revision/publication/build identities,
and artifact hashes. The result file is finalized atomically after writes.

Process exit codes: 0 only when load, observation and requested captures finish;
2 for request/compatibility failure; 3 for load/runtime failure; 4 for observation/
capture failure; 5 for cancellation. A native exit 0 alone is not parity success:
the managed comparison must also pass and verify complete artifact identities.

## 8. Controlled Profile And Comparison

The base fixture is the PRD's 100-node / 1,000-entry project. A smaller smoke
scene may aid development but cannot replace release qualification. Use an
explicit authored PerspectiveCamera and its full transform/FOV/near/far values,
1920x1080 output, matched Release artifacts, conventional directional shadows,
fixed 1/60-second simulation steps, random seed 0 where randomness is present,
and no editor-only overlays, debug shading, capture-provider overlays, restored
example scene, or synthetic replacement sun/material.

Reset scene/render histories before each controlled run. The base fixture is authored with manual exposure EV 9.7 and ACES fitted tone
mapping. Its static comparison preserves those authored settings; warm up 120
scene frames and capture frame 120 in each process. An arbitrary selected scene
retains its saved environment values; use the auto-exposure protocol for its
auto mode rather than overriding authored settings to obtain a passing image.
Record the effective setting snapshot so hidden CVar/startup overrides cannot
explain a mismatch. Restore the user's view afterward without dirtying assets.

Semantic tolerances:

- IDs, references, hierarchy, enums, booleans and array membership: exact.
- Floating scalars/vectors: absolute error <= 1e-4 or relative error <= 1e-4.
- Quaternion orientation: angular difference <= 0.01 degree, accounting for
  equivalent opposite-sign quaternions.
- Every required field is compared, including values not visually obvious in
  the base fixture. Missing observations or unsupported fields fail the gate.

Image comparison uses normalized sRGB RGB channels after identical output
conversion: whole-image RMSE <= 0.01 and 99th-percentile absolute channel error
<= 0.03. Only the outermost one-pixel border is excluded; no content-dependent
mask, automatic rebaseline, or omission of failed geometry is permitted. Keep
both original images and metrics for user inspection.

A field-coverage suite changes every editable camera/light/environment/material
field through editor UI or the same command service, saves, cooks, loads, and
compares semantic values. Camera framing and representative geometry/material/
lighting/atmosphere changes also have visible before/after checks. Test all
supported tone mapper and exposure modes. Auto-exposure cases reset history and
compare effective exposure at scene frames 120, 240, and 600 within 0.05 EV,
using the same luminance input/profile; static-image manual-exposure success
cannot substitute for this behavior. Each case states the affected field and
expected effect before running.

Thresholds and profiles are acceptance decisions, not claims about current
results. A failed run is fixed or requires an explicit design change; it is not
made green by silently relaxing thresholds or choosing different scenes.

### Saved-Revision Embedded Capture Session

Only one capture session may own the embedded runtime at a time. It records
operation ID, runtime run ID, document/scene/view lifetimes, saved revision,
profile and view settings before synchronizing the saved snapshot. All later
scene mutations for that target, including hierarchy changes, property changes,
asset assignments and environment updates, stay pending rather than changing
the captured projection. Authoring commits and revisions continue normally.
The viewport labels the saved revision being captured and the presence of newer
pending edits. Navigation and view-profile changes in that viewport are disabled
with capture-phase feedback during the bounded warm-up/observation/capture window;
input cannot silently move the comparison camera.

The session ends after the embedded artifacts are finalized, without waiting
for the standalone child when that child no longer needs the embedded runtime.
On success, cancellation or failure, remove the temporary capture profile and
restore the current document/view intent only if their lifetimes still match.
Synchronize a coherent current authoring snapshot, supersede pending work at or
before that revision, and apply only newer valid work before reporting the preview
current. Do not simply restore the old scene snapshot or blindly replay old edits.

Scene activation, document close, view destruction and runtime replacement cancel
and invalidate the capture session. A late capture callback cannot restore an old
view or scene into a new activation. The new active document follows its normal
full-sync path. Runtime failure leaves authoring intact and preview visibly pending/
unavailable; restart converges to current state. Release capture ownership on every
terminal path; the independent published-output lease remains until native reads
finish. The 120-second validation timeout also bounds capture-session waits.

## 9. UI And Process Behavior

Camera selection uses the selected authored PerspectiveCamera, otherwise the
sole authored perspective camera. If several exist, the action requires an
explicit camera choice; if none exists, it asks the user to create one. No
implicit editor-navigation camera is used as authored camera truth.

The scene's `Validate in Standalone` action is enabled for a saved scene with
validated published dependencies and matched tooling. Preflight explains any
blocking condition with a direct action such as Save or Cook. While running,
show the phase and Cancel; keep authoring responsive and report the snapshot
revision being validated. Success/failure uses existing operation/output surfaces
with copyable artifact paths. There is no additional validation-center panel.

Use structured process arguments, an operation-owned working directory, and no
shell command concatenation. Cancellation stops only the launched validation
child, waits for termination and I/O drain, then releases its output lease.
A 120-second process timeout is a visible validation failure, not a pass or an
infinite engine-frame wait. Failure must not change the author's scene or saved
files. No fallback to example content, another camera, or another engine build.

## 10. Persistence And Round Trip

Request/results/captures are local derived evidence under `.oxygen`; they are
not authored scene content. Publication and source hashes make evidence
reproducible. The coordinator holds the output lease until native reads finish,
so another cook cannot replace roots mid-validation. Evidence from a different
publication or edited source remains historical and cannot close the current gate.

## 11. Runtime Boundaries

Native validation uses the engine AssetLoader, scene instantiation, and renderer.
Embedded capture passes through Runtime/Interop stable capabilities. Headless
loading may prove semantic checks, but a headless/null-renderer run cannot prove
visual parity. The target CLI accepts arbitrary qualified project output; users
never copy their scene into the engine examples directory.

## 12. Diagnostics

Distinguish request/schema/build mismatch, missing output, bad index, unresolved
asset, unsupported required field, load failure, observation failure, capture
failure, semantic mismatch, image mismatch, timeout, and cancellation. Report
partial artifacts on failure. Logs support the result and do not replace it.

## 13. Dependencies

ContentPipeline reads project/document/publication contracts. RenderScene reads
only the request, expected identity mapping, and cooked output through engine
APIs; it has no WorldEditor/WinUI dependency. Expected-state generation is an
editor adapter, not engine authoring policy.

## 14. ED-M08 Validation Gates

- [ ] Exact project-root/scene requests work without checked-in example content,
  name matching, persisted UI selection, or default-camera/sun substitutions.
- [ ] Mismatched artifacts/requests fail before unsafe native loading.
- [ ] Every required field passes saved/cooked/observed semantic comparisons.
- [ ] Controlled static and auto-exposure/field-coverage visual cases pass.
- [ ] Edit/hierarchy change during warm-up does not contaminate the saved-revision
  image; afterward preview converges to the newer revision and the result remains
  labeled for its captured snapshot. Navigation cannot move the pinned camera.
- [ ] Cancel/fault/activation/close during capture releases ownership and cannot
  restore stale scene/view state, including late capture callbacks.
- [ ] Missing assets, invalid index, wrong root/scene, unsupported fields, stale
  publication, cancellation, timeout and child crash produce precise failures.
- [ ] Output leasing blocks replacement during native reads and is released on
  all terminal paths; authoring and saved files remain unchanged.
- [ ] User review of the qualification artifacts is recorded once in the ED-M08
  ledger row; native exit success alone never closes parity.

## 15. Scope Decisions

The user entry point is editor UI; execution uses the specified exact-request
RenderScene CLI. The request/result format, comparison profile, error tolerances,
artifact ownership and gates are decided above. Implementation is pending and
owned by the ED-M08 plan; no design placeholder authorizes a shortcut.
