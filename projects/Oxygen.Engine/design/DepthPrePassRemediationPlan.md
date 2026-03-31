# Depth PrePass Remediation Plan

Status: `in_progress`
Audience: renderer engineers remediating Oxygen's depth prepass and its
downstream products
Scope: fix the issues identified in the March 31, 2026 depth-prepass review,
one issue at a time, with UE5 closeness treated as an explicit success factor
and validation criterion

This plan is intentionally strict. The goal is not to "improve depth prepass a
bit." The goal is to move Oxygen's depth path toward the same architectural
shape and exploitation model that makes UE5's early depth path valuable.

## 1. Non-Negotiable Rules

Every remediation phase must satisfy all of the following before it can be
called complete:

1. Implementation exists in Oxygen code.
2. Relevant docs are updated.
3. Validation evidence is recorded.
4. UE5 closeness is evaluated explicitly, not assumed.

If a phase changes design scope, update this plan first. Do not bury scope
changes inside code.

## 2. Success Criteria

The remediation succeeds only when all of these are true:

- the pass contract is correct and explicit
- the pass is specialized like UE5 where the material and geometry allow it
- later passes consume shared depth products instead of rebuilding private
  derivatives
- the engine can express and validate different prepass policies
- the depth path has measurable evidence through repo-owned tooling
- the implementation is demonstrably closer to UE5 in pass shape, not just in
  comments or naming

## 3. UE5 Reference Anchors

Primary UE5 references for this work:

- `F:/projects/UnrealEngine2/Engine/Source/Runtime/Renderer/Private/DepthRendering.cpp`
- `F:/projects/UnrealEngine2/Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp`
- `F:/projects/UnrealEngine2/Engine/Source/Runtime/Renderer/Private/BasePassRendering.cpp`
- `F:/projects/UnrealEngine2/Engine/Source/Runtime/Renderer/Private/SceneVisibility.cpp`
- `F:/projects/UnrealEngine2/Engine/Shaders/Private/DepthOnlyVertexShader.usf`
- `F:/projects/UnrealEngine2/Engine/Shaders/Private/DepthOnlyPixelShader.usf`
- `F:/projects/UnrealEngine2/Engine/Shaders/Private/HZB.usf`

The relevant UE5 characteristics to track during implementation are:

- explicit early-Z policy modes
- explicit "early depth complete" downstream contract
- position-only depth path
- null-pixel-shader path when materials allow it
- PS path only for masked / pixel-depth-offset / coverage cases
- shared HZB/depth products used by later systems
- reversed-Z depth convention

## 4. Phase Order

The order below is deliberate. Do not skip ahead because a later phase looks
more interesting.

### Phase DP-0: Fix Measurement And Validation Surface

Problem:
- current RenderDoc pass timing sees `DepthPrePass` markers but no nested work
  in sampled replay-safe captures, so the pass cannot be measured reliably
  through the existing pass-timing script

Goal:
- depth prepass work must be visible and measurable in repo-owned analysis
  artifacts

Implementation work:
- audit marker placement and pass scope naming for `DepthPrePass`
- ensure renderdoc event scopes cleanly wrap the actual clear and draw work
- add a focused depth-prepass analysis workflow under
  `Examples/RenderScene/` if the generic pass-timing script is insufficient
- document the exact capture-and-analysis recipe for steady-state late frames

Validation:
- replay-safe late-frame `Release` captures
- RenderDoc report showing non-empty depth-prepass work events
- before/after evidence proving that timing and event counts are stable enough
  to evaluate later optimizations

UE5 closeness check:
- UE5 has measurable depth pass and explicit downstream consequences
- Oxygen must at least make the pass observable enough to support the same kind
  of design reasoning

Status: `not_started`

### Phase DP-1: Fix The Viewport/Scissor Contract

Problem:
- `SetViewport()` and `SetScissors()` store overrides but
  `SetupViewPortAndScissors()` ignores them and always binds the full target

Goal:
- the pass must render exactly the intended rectangle, and later passes must
  know what rect is valid

Implementation work:
- honor stored viewport/scissor overrides in `DepthPrePass`
- define the effective depth rect as part of the pass output contract
- update any clear behavior that incorrectly assumes full-target coverage
- add tests for full-target and sub-rect cases
- update the depth-prepass documentation to match the real behavior

Validation:
- unit/integration tests for viewport and scissor honoring
- RenderDoc evidence from a clipped-rect scenario
- doc update in `src/Oxygen/Renderer/Docs/passes/depth_pre_pass.md`

UE5 closeness check:
- UE5 reasons about per-view rects explicitly during depth and HZB work
- Oxygen must stop treating full-target rasterization as the only legal mode

Status: `not_started`

### Phase DP-2: Introduce A Real Depth Products Contract

Problem:
- `DepthPrePass` effectively publishes only a raw depth texture, and each
  consumer rebuilds SRVs and private assumptions

Goal:
- depth prepass should publish a stable, reusable depth-products package

Implementation work:
- design a `DepthPrePassOutput` or `SceneDepthProducts` object
- include at minimum:
  - depth texture
  - canonical SRV or descriptor identity
  - dimensions
  - effective viewport/scissor rect
  - depth convention metadata
  - explicit completeness or validity flags
- migrate `ScreenHzbBuildPass`, `LightCullingPass`, `VsmPageRequestGeneratorPass`,
  and `ShaderPass` to consume the shared output instead of private lookup paths
- remove duplicated SRV-ownership logic where possible

Validation:
- tests proving shared output correctness
- code review evidence showing removed duplicate SRV creation paths
- RenderDoc validation that later passes still bind the correct depth resources

UE5 closeness check:
- UE5 treats scene depth as a first-class renderer product, not a raw texture
  each pass discovers independently

Status: `not_started`

### Phase DP-3: Specialize The Depth Pass Like UE5

Problem:
- Oxygen always binds a pixel shader and always loads a full vertex payload,
  even for fully opaque depth-only draws

Goal:
- specialize depth rendering into the same broad classes UE5 uses

Implementation work:
- split depth PSOs into at least:
  - position-only, no-pixel-shader
  - full-vertex, no-pixel-shader
  - full-vertex plus pixel-shader for masked / PDO / coverage-required cases
- introduce any required geometry metadata changes to support position-only
  vertex streams or equivalent optimized fetch paths
- avoid interpolants and material fetches for opaque no-PS depth draws
- define clear eligibility rules for each permutation
- document those rules

Validation:
- new tests covering permutation selection
- replay-safe captures showing the specialized event shape
- measured before/after evidence on representative scenes

UE5 closeness check:
- explicitly compare Oxygen specialization against:
  - `TDepthOnlyVS<true>`
  - `TDepthOnlyVS<false>`
  - `DepthPosOnlyNoPixelPipeline`
  - `DepthNoPixelPipeline`
  - `FDepthOnlyPS` only when needed
- this phase is not complete unless Oxygen's pass shape is recognizably in the
  same family as UE5's

Status: `not_started`

### Phase DP-4: Add A Depth-Prepass Policy Surface

Problem:
- Oxygen always runs the depth pass whenever a depth texture exists, with no
  policy surface comparable to UE5's early-Z modes

Goal:
- the renderer must be able to choose, validate, and reason about prepass mode

Implementation work:
- introduce a policy surface for depth prepass mode
- minimum candidate modes to evaluate:
  - off
  - opaque only
  - masked plus opaque
  - occluders only
  - full depth prepass with velocity-aware behavior if relevant
- define what "early depth complete" means in Oxygen
- publish that completeness state to later passes
- ensure the pipeline does not infer completeness from mere pass execution

Validation:
- policy-selection tests
- scene-level validation showing which passes run under each mode
- documentation covering each mode and its intended use

UE5 closeness check:
- compare the final mode set and completeness semantics against UE5's
  `EarlyZPassMode` and `bIsEarlyDepthComplete`

Status: `not_started`

### Phase DP-5: Rework Downstream Consumers To Exploit Depth Better

Problem:
- later passes consume depth passively instead of exploiting the fact that a
  prepass already happened

Goal:
- later passes should derive clear value from the prepass

Implementation work:
- tighten `ShaderPass` dependency and read-only-depth contract
- evaluate depth-equal opportunities where the depth prepass is complete
- decide whether stencil sideband data is worth generating and consuming
- review VSM page request generation, light culling, and any other passes that
  could consume richer depth outputs instead of raw depth

Validation:
- targeted tests for later-pass behavior under complete vs incomplete depth
- RenderDoc evidence showing intended depth-state transitions and bindings
- measurable benefit or explicit rationale for each adopted downstream use

UE5 closeness check:
- compare downstream exploitation against UE5's base-pass and scheduling usage
  of early depth completeness

Status: `not_started`

### Phase DP-6: Unify Depth Derivatives And Stop Redundant Work

Problem:
- HZB, tile min/max, and raw-depth consumers are fragmented; later systems are
  still rebuilding private derivatives

Goal:
- define one coherent set of scene-depth derivatives and route consumers to
  those shared products

Implementation work:
- decide the canonical derivative set:
  - raw depth
  - HZB
  - tile min/max or cluster-friendly min/max products
  - any other depth-derived products that later passes need
- evaluate whether Oxygen should add closest plus furthest HZB products like
  UE5 instead of the current min-only pyramid
- route light culling and VSM request generation through shared products where
  it reduces work or improves robustness
- remove derivative duplication from individual passes

Validation:
- pass-level timing before/after for derivative-producing and derivative-
  consuming passes
- correctness validation on scenes that stress culling and VSM
- updated documentation for scene-depth product ownership

UE5 closeness check:
- compare the final product set and consumer routing against UE5's HZB and
  depth-derived auxiliary products

Status: `not_started`

### Phase DP-7: Evaluate And Potentially Migrate To Reversed-Z

Problem:
- Oxygen still uses forward-Z conventions, which weakens depth precision for
  HZB, clustered culling, and VSM-related depth consumers

Goal:
- decide whether reversed-Z should become the engine convention, and if yes,
  migrate correctly instead of patching pass-local math

Implementation work:
- perform a design-first audit of all impacted systems:
  - projection setup
  - clear values
  - comparison ops
  - HZB reduction semantics
  - light culling depth conversion
  - shadow/VSM consumers
  - any CPU-side culling or debug tooling assumptions
- if approved, stage the migration in documented sub-phases
- update all shared depth metadata so downstream passes know the active
  convention

Validation:
- explicit precision/correctness evidence
- renderer-wide regression tests where available
- RenderDoc validation of the updated depth semantics

UE5 closeness check:
- compare convention and dependent formulas against UE5's inverted-Z path

Status: `not_started`

### Phase DP-8: Clean Up Tests, Docs, And Ownership Boundaries

Problem:
- current docs are stale, dedicated tests are thin, and pass registration /
  ownership semantics are messier than they should be

Goal:
- the depth path should be understandable, testable, and cleanly owned

Implementation work:
- add dedicated depth-prepass tests instead of relying only on indirect tests
- update `depth_pre_pass.md` to the actual architecture
- clean up ownership/registration semantics if double-registration remains
- document the final relationship between depth prepass, HZB, light culling,
  VSM, and shader pass

Validation:
- focused tests for depth-prepass behavior and contracts
- doc review against actual implementation
- final audit that this remediation plan is fully reflected in repo docs

UE5 closeness check:
- this phase is only complete when the Oxygen docs accurately describe the
  adopted UE5-inspired structure, not the old simplified one

Status: `not_started`

## 5. Validation Matrix

Every implementation phase should record evidence in
`out/build-ninja/analysis/depth_prepass_*` with exact commands and artifacts.

Minimum evidence categories:

- code-level tests
- replay-safe `Release` RenderDoc captures
- pass-local timing where relevant
- focused deep-dive reports where timing alone is insufficient
- before/after documentation updates

Recommended capture discipline:

- use late frames only
- use `Release`
- use the repo-owned RenderDoc UI analysis workflow
- keep baseline analysis bounded and put expensive inspection into focused
  scripts

## 6. Explicit Non-Shortcuts

The following are not acceptable ways to close this work:

- micro-tuning shader instructions while keeping the wrong pass shape
- adding one-off depth SRVs in more passes instead of introducing a shared
  output contract
- calling the pass "UE5-like" without matching the specialization and policy
  model in substance
- declaring reversed-Z out of scope without a documented evaluation
- updating code without updating docs and validation evidence

## 7. Exit Condition

This plan remains `in_progress` until:

- every phase above is either completed with evidence or explicitly descoped
  with a documented rationale
- the final Oxygen depth path is measurably closer to UE5 in structure,
  specialization, and downstream exploitation
- the repo-owned tooling can validate the resulting behavior on replay-safe
  captures
