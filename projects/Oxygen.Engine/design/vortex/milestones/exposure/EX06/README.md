# EX06 — Authoring and persistence

Status: `validated`

| Field     | Summary                                                                           |
| --------- | --------------------------------------------------------------------------------- |
| Outcome   | Canonical source/cook/load/script/editor persistence and configuration isolation. |
| Remaining | None in the recorded scope.                                                       |
| Evidence  | [Record below](#ex06--authoring-and-persistence)                                  |

[Roadmap](../../../PLAN.md) · [Design index](../../../lld/README.md)

## Delivery scope

**Closed 2026-09-21.** Source/cook/package/load/script/editor integration, strict
current-format cutover, C++20 editor boundary and rendered DemoShell acceptance
are qualified. The [item tracker](#tasks-and-outcome)
and [local evidence](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice6/slice6-progress.json)
record validation. The TexturedCube assignment/panel-refresh regression is
unit-tested and confirmed fixed by the user's rebuilt-app test. Its deferred console
work subsequently closed in [EX08.1](../EX08.1/validation.md). ImGui Test Engine
integration also closed in [EX08.2](../EX08.2/validation.md); its earlier deferral
is superseded.

- Include native aperture/shutter/ISO source/cook/load persistence, as
  approved on 2026-09-16. Scene-v6 perspective/orthographic records are 32/40
  bytes. Older source/asset versions are rejected; new cameras default to
  11/125/100. Existing editor
  adapters preserve the fields; physical-camera editor controls stay deferred.
- Update source JSON schemas, scene component/config types, versioned packed
  records, cooker, loader, scripting and existing editor/native adapters.
- Round-trip mask resource references, curve keys, black influence, D and
  every existing exposure field; preserve enum ordinals and current authoring defaults.
- Add async mask residency/error behavior and atomic settings revision changes.
- Update DemoShell controls and labels; expose requested/effective exposure
  separately and show resource/metering failures.
- Add the LightBench experiment-owned activation policy so saved settings
  cannot override its camera, scene or post-process recipe.

#### Repository-wide strict format cutover

**User direction, 2026-09-21: no backward compatibility or retained legacy code.**
This supersedes the original v5-hydration contract. Scene descriptor sources and
cooked scene assets use v6 only. Do not retain v5 readers, default-hydration
branches, alternate record types or compatibility shims. Obsolete inputs fail
with concise, actionable migration/re-cook diagnostics.

Synchronize all affected producers and consumers before closure: native packed
records and Serio code, cooker/PAK dependency remapping, Content and SceneAsset
loaders, PakGen/PakDump/Inspector and their schemas, scripts/fixture generators,
managed/editor/scripting adapters, every example/demo and shipped scene source.
Regenerate affected current-format generated/golden assets through their owning
tools. Retain old-version byte samples only as rejection tests, never acceptance
fixtures or a supported read path. Verify each affected surface against the new
layout and record the checks; a successful engine build alone cannot close this
repository-wide requirement.

PakGen source specifications use v7 only; obsolete v6 acceptance and older
version aliases are removed. Scene payloads use v6 independently of the PAK
container/specification version. The editor dependency snapshot and manifest
flow must include authored metering-mask texture descriptors and their image
sources, so masks cannot disappear between scene save and native cooking.

PAK inputs use the same resource/dependency planning path as loose sources.
Directory-only projection is not a supported repacking path. Use bounded source-file slices for current resource tables and
payloads, retain script parameter ownership, and remap source-local references
after final placement. Reserve index zero when patch filtering removes an
input's null record. Qualify loose-to-PAK-to-PAK mask preservation and multiple
input sources; do not emit a successful package with missing resource regions.
Script parameter payloads must follow the same owner/slot order as patch slot
records, even when source offsets use a different physical order. The public
PakGen file APIs must validate the supplied specification version without
replacing it before validation. Current descriptor versions are material 2,
geometry 1 and scene 6, matching their native definitions.

#### Approved editor SDK boundary repair

The review approved repairing engine public headers on 2026-09-21 after the
installed compiler confirmed that C++/CLI cannot consume C++23 declarations.
Use the existing typed `oxygen::Result` for public exposure validation results.
Expose submission callbacks through a C++20-compatible, move-only owning callback
with inline storage; preserve exactly-once submission/discard behavior and
exception isolation. Lift published environment diagnostics out of the private
SceneRenderer definition so Renderer clients do not include retained-pool internals.
No C++/CLI language-mode workaround, shared-ownership conversion, legacy overload
or warning suppression is permitted. Qualify lifetime behavior with the existing
submission tests and the installed editor build.

#### Scripting exposure transport

Post-process `get`/`set` round-trip every authored exposure scalar and ordered
curve key. `set` validates one complete candidate and rejects it without partial
mutation. It returns `false, reason` and logs one warning for an invalid edit.
ManualCamera defers camera-dependent gain validation to the active view.
The runtime `auto_exposure_metering_mask` field carries the existing uint64
ResourceKey as a decimal string (`"0"` clears it), preserving all identity bits
across Luau's numeric boundary. This is a runtime handle representation, not a
persisted GPU descriptor or asset UUID. Source descriptors continue to author a
texture virtual path, and cooked scenes continue to store a source-local index.

LightBench startup must author its camera pose, post-process state and a usable
reference light explicitly. Experiment-owned activation cannot depend on saved
camera/environment state to make the reference cards visible. Its scene-control
panel follows the same transient ownership policy. This establishes a stable
initial scene; it does not implement or qualify the later experiment registry.
LightBench must publish a persistent main-view state, using the existing
RenderScene lifetime pattern: retire the old publication when the scene/camera
owner changes and at shutdown. A stateless view cannot support the authoring
panel's accepted-state reporting or temporal automatic adaptation.

#### DemoShell user scenarios and implementation quality

The acceptance run also qualifies ordinary demo startup at warning verbosity.
Demo applications resolve hot-reload scripts from their existing example content
root. Missing optional profiling CVars and valid scenes without a directional
sun are verbose diagnostics, not warnings. Explicit source failures remain
warnings/errors; no console feature or Test Engine integration is added here.

EX06-08/09 reviews the logic and UI through complete editing scenarios. Review the existing native panel and
activation/settings owners together, using fresh rendered evidence for visual
claims. Keep the existing DemoShell design language and reduce cognitive load.

- Open an authored scene: make the current mode, effective behavior and ownership
  understandable without repeating the same values in several places.
- Change mode or exposure: show relevant controls and units, give predictable
  feedback, and preserve valid settings while a coupled edit is invalid.
- Request a mask: distinguish pending, accepted and failed resources; explain
  which settings remain active and provide a clear recovery action.
- Switch scenes or return to an ordinary demo: apply the correct scene/user
  ownership policy without stale overrides or unexpected resets.
- Activate or reset an experiment: apply the complete recipe coherently, keep
  unrelated personal preferences, and make reset scope clear.
- Assign textures, import another texture and switch panels: keep source-qualified
  assignments reloadable, and avoid cache invalidation for an unchanged browser
  refresh. Same-path external-file extent updates refresh metadata; conflicting
  file mappings retain their collision policy.
- Run a batch: preserve personal settings and avoid requiring UI intervention.

Use progressive disclosure for advanced controls such as transition distance,
consistent alignment/spacing, and precise labels. Verify keyboard interaction,
focus, narrow panel layouts, disabled controls and error/status presentation in
the actual UI; do not infer visual usability solely from source code.

Use named constants for meaningful limits, shared defaults and wire contracts,
not incidental literals. Apply schema validation where expressible and canonical
validation for coupled/runtime constraints. Reject nonfinite/out-of-range values,
invalid enum values and malformed packed boundaries before partial application.
Any new enum follows repository naming, underlying-type and ADL `to_string`
pretty-printing conventions, including unknown-value behavior.

Log meaningful actions/state changes at INFO, recoverable rejections at WARNING,
and failures at ERROR. Include concise asset/view/revision, field and reason
context, and whether prior accepted state was retained. Reuse existing diagnostic
owners; avoid per-frame repetition, logs for each drag sample, duplicate logging
across layers, and serialized settings dumps.

**Gate:** source -> cook -> load -> runtime and save/reload retain identical
resolved settings. Every affected tool and demo uses the new format; obsolete
versions/layouts are rejected without compatibility code. Batch runs do not mutate
personal settings.

## Recorded qualification

| Slice | Status | Boundary | Evidence |
| ----- | ------ | -------- | -------- |

| 6 — Authoring and persistence | validated | Strict source/cook/load/script/editor migration, C++20 editor boundary, PAK repacking, rendered UI acceptance and configuration isolation closed. | [Detailed items](#tasks-and-outcome), [acceptance evidence](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice6/slice6-progress.json) |

## Tasks and outcome

**Slice status: validated; EX06-GATE closed 2026-09-21.**

| ID        | Delivered result                                                                  | Status    |
| --------- | --------------------------------------------------------------------------------- | --------- |
| EX06-01   | Native physical-camera persistence and existing editor adapters                   | validated |
| EX06-02   | Canonical exposure schemas and complete-candidate validation                      | validated |
| EX06-03   | Current packed exposure record and bounded curve serialization                    | validated |
| EX06-04   | Source-qualified texture identity, cooker/package remapping and loaders           | validated |
| EX06-05   | Scripting and C++20-compatible editor/native transport                            | validated |
| EX06-06   | Current-format round-trip, tooling/fixture migration and obsolete-input rejection | validated |
| EX06-07   | Asynchronous mask loading, atomic acceptance and GPU upload handoff               | validated |
| EX06-08   | DemoShell controls/status, rendered UX and reloadable demo texture assignments    | validated |
| EX06-09   | LightBench experiment ownership and personal-settings isolation                   | validated |
| EX06-GATE | Authoring, persistence, migration and isolation acceptance                        | validated |

**Evidence:** [slice acceptance](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice6/slice6-progress.json),
[native validation index](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice6/slice6-validation-index.json),
[TexturedCube regression and user confirmation](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice6/texturedcube-regression.json).
The [plan](#delivery-scope)
retains the wire/API contracts and
[UX requirements](#demoshell-user-scenarios-and-implementation-quality).
No EX06 delivery item remains open.
