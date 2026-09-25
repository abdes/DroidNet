# V0.1 authoring contract for ED-M08

Status: **final implementation contract**

## 1. Purpose and authority

V0.1 is a static-scene and scalar-material authoring release with a complete
edit → history → Save → cook → native load → render path. Every exposed control
has an implemented effect. Runtime availability and diagnostic observations are
separate from authored state.

[ED-M08](../plan/ED-M08-runtime-parity-and-standalone-validation.md) schedules
implementation and qualification. [PRD sections 8–10](../PRD.md) define the release
envelope. The domain LLDs below own exact fields, defaults and conversion rules;
this contract fixes the capability boundary and cross-domain behavior.

## 2. Capability matrix

| Capability                    | V0.1 contract                                                                                                                                                         | Detailed owner                                                                                             |
| ----------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| Hierarchy and transforms      | Stable IDs; create/delete/duplicate/rename/reparent; local position/rotation/scale; mixed selection; explicit handling of unrepresentable transforms                  | [Scene model](../lld/scene-authoring-model.md), [property pipeline](../lld/property-pipeline.md)           |
| Primitive creation            | Cube, Sphere, Capsule, Cylinder, Cone, Plane, Quad, IcoSphere, Torus; SubdividedCube under Advanced; engine-owned recipes/defaults                                    | [Inspector](../lld/property-inspector.md), [content pipeline](../lld/content-pipeline.md)                  |
| Mesh materials                | Every existing slot supports an independent instance override; Clear restores the mesh default; stable SlotId and explicit repair of lost continuity                  | [Content pipeline](../lld/content-pipeline.md), [material editor](../lld/material-editor.md)               |
| Scalar materials              | Base colour/opacity, metallic, roughness, alpha mode/cutoff, sidedness and emission colour/HDR intensity; conditional texture-dependent controls only when meaningful | [Material editor](../lld/material-editor.md)                                                               |
| Node visual state             | Local/Inherit source modes; roots resolve Shown and geometry casting/receiving On; children Inherit; explicit child overrides are honored                             | [Visibility contract](ED-M08-node-light-visibility-review.md)                                              |
| Editor Hide                   | Local per-user/project main-view mask; retains illumination and caster eligibility; no authored dirty/history/cook effect                                             | [Settings](../lld/settings-architecture.md), [visibility contract](ED-M08-node-light-visibility-review.md) |
| Cameras                       | Basic perspective pose, vertical FOV, near/far and Auto/Fixed framing; exact authored selection independent of navigation                                             | [Inspector](../lld/property-inspector.md), [runtime](../lld/runtime-integration.md)                        |
| Directional lighting          | Realtime contribution, colour/lux, atmosphere disk diameter, independent shadowing and retained advanced shadow/CSM/contact controls with real effects                | [Inspector](../lld/property-inspector.md), [environment](../lld/environment-authoring.md)                  |
| Atmospheric lights            | Per-light None/Primary/Secondary; two simultaneous contributing/shadowed sources; ordinary role-None directional illumination; no implicit promotion                  | [Celestial contract](ED-M08-celestial-light-authoring.md)                                                  |
| Sky/environment               | Atmospheric sky and captured-sky diffuse/specular lighting, Manual/Auto exposure, all retained tone mapper/grade/bloom/background controls                            | [Environment](../lld/environment-authoring.md)                                                             |
| Publication and live delivery | Explicit source Save, incremental cook/publication, typed references, visible failure/pending state and coherent current-state convergence                            | [Cooking workflows](../lld/content-cooking-workflows.md), [live sync](../lld/live-engine-sync.md)          |

The field tables retain native-supported advanced shadow/atmosphere/exposure
controls. A missing cook or GPU consumer is implementation work. Mixed/Baked
light authoring is absent because V0.1 has no baking workflow; it must not appear
as a second label for realtime behavior. New scene/environment defaults are
explicit in the owning tables, separate from the qualification profile.

### Primitive geometry defaults

All dimensions are metres and pivots are centred. Native recipes produce these
buffers directly; the editor applies no corrective scale or rotation.

| Primitive             | Size                                    | Orientation                                        |
| --------------------- | --------------------------------------- | -------------------------------------------------- |
| Cube / SubdividedCube | 1 m edges                               | Axis-aligned                                       |
| Sphere / IcoSphere    | 1 m diameter                            | Z-axis poles where applicable                      |
| Cylinder / Cone       | 1 m height and diameter                 | Long axis Z; cone tip +Z                           |
| Capsule               | 2 m total height, 1 m diameter          | Long axis Z                                        |
| Torus                 | 1 m outer diameter; 0.2 m tube diameter | Ring in XY; major radius 0.4 m, minor radius 0.1 m |
| Plane                 | 1×1 m                                   | XY ground surface; front +Z                        |
| Quad                  | 1×1 m                                   | XZ upright card; front −Y                          |

ArrowGizmo is an internal tool resource. IcoSphere uses one canonical identity
through authoring, cooking and loading. Every selectable primitive
must cook and render. Source/API prose describes actual parameters, axes,
returns and defaults and changes with the implementation.

### Camera framing

Auto is the creation default. It derives aspect per target with unchanged
vertical FOV and no saved-data mutation. Fixed uses the authored ratio and
centred letterbox/pillarbox composition without cropping or stretching. Bars
are outside scene metering/post-processing. Editor navigation remains independent.
Imported explicit glTF ratios map to Fixed; omitted ratios map to Auto. Useful
prior explicit Oxygen ratios migrate to Fixed.

### Materials and slot identity

Overrides belong to a scene instance and target geometry identity plus opaque
SlotId. Mesh topology and slot creation are not editor operations in V0.1.
Importer provenance proves continuity; changed material parameters do not by
themselves change slot identity. Ambiguous structural changes require explicit
repair instead of guessed name/index rebinding. The content LLD defines the
layout witness, publication barrier and repair transaction.

Emission uses linear RGB colour and a nonnegative HDR multiplier; zero disables
emission without erasing colour. Native float32 emissive RGB preserves the
comparison precision. Emission is self-illumination; it does not add emissive GI
or an Unlit material mode. The material LLD owns the representation and bounds.

### Environment and output

Captured sky lighting consumes radiance from the sky/atmosphere, including both
assigned directional contributors. The display-only clear background is not an
IBL source. With no lighting sky, captured sky illumination is zero and stale
products are invalidated. Background colour retains its display-colour meaning.

Manual/Auto exposure and retained grading parameters have defined equations,
ordering and conditional UI in the environment contract. None bypasses the tone
curve only; it does not silently disable other active controls. Physical-camera
editor controls are outside V0.1 even though native camera exposure already exists.

## 3. Development-only qualification

Qualification uses opt-in native/managed targets and an isolated build/evidence
root. Normal Debug and Release editor/RenderScene builds and SDK/packages contain
no qualification runner, protocol, fixtures, comparison UI or instrumentation.
Production fixes remain in their real owners. Development hooks drive/observe the
same algorithms and compile out of normal builds.

The [standalone LLD](../lld/standalone-runtime-validation.md) defines exact targets,
protocol, saved-input proofs, admission, frame/readback ownership, cancellation
and image/semantic algorithms. The full workload is 100 nodes/1,000 logical entries,
with separate bounded field cases. Numerical/image thresholds are fixed; source
expectations are independent of native observations. Native rendered verification
precedes editor reliance on the changed capabilities.

## 4. Canonical migration and excluded scope

There is one current source/cooked/runtime contract. Maintained development tools
migrate useful prior content, update references and recook through normal native
producers. Atomic backups/recovery protect migration; they do not introduce
legacy runtime readers, alias resolution or duplicate behavior paths.

V0.1 excludes generic node activation, authored hidden-shadow/Shadows Only modes,
blended-material shadow casting, more than two atmospheric sources, sky-only
authoring, lunar surface/phase/orbit features, physical/orthographic camera
editor authoring, texture/material-graph authoring, topology/slot creation,
physics/script authoring and stable multi-viewport UX.

The [engine deferred-capability record](../../../projects/Oxygen.Engine/design/vortex/milestones/ED-M08/deferred-capabilities.md)
owns source-local TODO IDs and distinguishes existing native functionality from
future authoring/persistence. No current M08 obligation is deferred merely because
its implementation is incomplete.

## 5. Eliminated alternatives

| Alternative                                                                  | Reason for elimination                                                                                                                         |
| ---------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| Treat every public engine field or existing UI registration as product scope | Fields need a useful authoring workflow, precise semantics and a complete consumer path; nonfunctional baking labels do not meet that contract |
| One exclusive scene Sun reference                                            | Cannot represent two simultaneous celestial contributors; per-light slots fit both dual-sun and sun/moon worlds                                |
| Rename Primary/Secondary to A/B                                              | Adds no runtime capability; existing names are retained with explicit stable-slot semantics                                                    |
| Collapse visibility, activation, contribution and shadows                    | They control different effects and lifetimes; the explicit Local/Inherit/contribution/shadow contracts preserve that separation                |
| Force ancestor-AND visibility everywhere                                     | Removes intentional local overrides from the native model                                                                                      |
| Retain slot0-only overrides or guess reimport matches                        | Restricts real multi-material meshes or sends overrides to the wrong surfaces                                                                  |
| Keep binary16 emission and relax comparison tolerances                       | Changes the precision contract; float32 compiled factors meet the established scalar requirement                                               |
| Make clear background colour illuminate the scene                            | Mixes display composition with environmental radiance and changes the meaning of colour selection                                              |
| Ship validation code behind a hidden menu or DEBUG flag                      | Normal Debug is also a shippable build; qualification has its own opt-in graph                                                                 |
| Keep old formats/aliases as compatibility paths                              | Multiplies runtime contracts; one-time source migration produces canonical content                                                             |

## 6. Acceptance

Implement and verify the field tables through command/history/Save/cook/native
load/render paths. Include changed slots, two atmosphere sources, Secondary alone,
visibility and shadow independence, Auto/Fixed views, emission, captured diffuse
and specular sky, all retained exposure/grade controls and migration failures.

The plan's eight slices, fixed qualification constants, normal-build exclusion
checks and joint rendered review define completion. Documentation readiness is
tracked separately from implementation and rendered qualification.
