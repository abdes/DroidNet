# Inspector refactoring with visual fidelity

Status: `landed_needs_validation` — implementation complete; reviewer in-editor validation pending.

| Outcome                                                                                                                                                                                                                                             | Remaining                                                                                                     | Evidence                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| IR-01–09 implementation is complete, including typed Environment sections, shared scene editing, scalar binding consolidation, Light presentation, Transform sessions, host collaborators and duplicate-control retirement. IR-V01–03 are resolved. | Reviewer validates appearance and usability inside the editor. No further screenshot/matrix work is required. | Debug/x64, .NET SDK `10.0.401`, MSBuild `18.10.1`. Full refactor Unit 224/224 and Unit.UI 226/226 passed before the final defect fixes. Pre-fix native Inspector/captured-sky 103/104; the failing role case and all four imported-content workflows now pass in the final targeted 6/6 run against the rebuilt Debug SDK. Strict dependency-report tests pass 5/5. App/test builds and scoped analysis passed; the full suites were not rerun after the last targeted fixes. |

This is a scoped maintenance implementation, not a reopening of delivered ED-M04
or a replacement for ED-M07A/07B/M08. It preserves existing authoring behavior
and does not qualify previously incomplete native capabilities.

### Completion ledger

`validated` below closes the named work only; a slice stays open until all its
acceptance gates are closed. IR-01–05 were committed as `1a0c8976d` with reviewer
authorization. The reviewer rebuilt/reinstalled the Debug SDK; this agent has not
built the native engine. Manual editor appearance acceptance is reviewer-owned.

| Work                                                                    | State     | Remaining / evidence                                                                                                                                                                                                                                                                                                                                                                |
| ----------------------------------------------------------------------- | --------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| IR-01 field inventory and behavioral baseline                           | validated | 42 scene cards, seven sections and nested-source identity inventory recorded; initial native Inspector UI 155/155.                                                                                                                                                                                                                                                                  |
| IR-02 declarative layout and disclosure behavior                        | validated | Runtime reparenting removed; ordered identities, default/temporary expansion, real header toggles, chevrons and visibility pass in the full current UI suite.                                                                                                                                                                                                                       |
| IR-03 headless search/applicability and replacement lifetime            | validated | Explicit metadata and immutable canonical identities; matching, expansion restoration, applicability, focus and model replacement covered by headless/native UI tests.                                                                                                                                                                                                              |
| IR-04 scalar implementation, gestures and composed-row parity           | validated | All 52 declared scalars migrated; six reference/composed geometry/typography cases and native caption reflow/reload pass. Light scalar comparisons are pixel-identical.                                                                                                                                                                                                             |
| IR-05 RGB, curve and geometry/material picker implementation            | validated | Shared controls and parent-owned curve policy built; RGB UI 53/53, curve/search unit 17/17, binding UI 36/36 and picker status/focus 7/7.                                                                                                                                                                                                                                           |
| IR-05 composed RGB geometry, curve input and picker invocation adapters | validated | Fourteen cases cover geometry, mixed channels, live accessory metadata, stable inputs, curve rejection/correction/remove/undo and typed original-row picker invocation. Ten Light-theme RGB reference/composed images are pixel-identical; geometry/material picker images are also identical. Curve layout/typography/preview match, with only border antialias alpha variation.   |
| Screenshot/matrix implementation gate                                   | removed   | Reviewer explicitly ended capture work. Durable UI assertions remain; the reviewer will validate appearance and usability inside the editor. This is not a claim that manual validation passed.                                                                                                                                                                                     |
| Full Unit and Unit.UI suites                                            | validated | Full coordinated refactor: Unit 224/224 and Unit.UI 226/226, no skips. These full runs preceded the final targeted defect fixes; no later full-suite run is claimed.                                                                                                                                                                                                                |
| Editor app / Integration.UI builds and scoped analysis                  | validated | Debug/x64 app and all owning projects build against existing installed-SDK references. Opt-in shared Controls and WorldEditor analysis succeeds; new controls, curve and presentation files have no reported diagnostics. Existing monolithic-model warnings remain.                                                                                                                |
| Native field/history/persistence and captured-sky workflows             | validated | The pre-fix targeted run passed 103/104, including all 80 component/environment field history/reopen cases. The remaining role case now passes alongside its companion and four import workflows in the final 6/6 run. IR-V01–03 are resolved.                                                                                                                                      |
| IR-06–09 implementation                                                 | validated | Five typed sections, one shared scene edit owner, consolidated Camera/Light binding state, shadow/orientation owners, Transform controller, factory/selection observer and duplicate-control retirement are implemented. Full refactor Unit 224/224 and Unit.UI 226/226 passed; final defect scenarios pass 6/6 on the rebuilt SDK. Reviewer in-editor acceptance remains separate. |

### Resolved qualification items

| ID     | State     | Next action / owner                                                                                                                                                                                                                                                                                                                                     |
| ------ | --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| IR-V01 | validated | Direct occupied-role assignment still rejects without history changes. Scene picker replacement is one atomic history entry; replay clears roles before assignments. Native replacement, undo, redo and saved reopen pass in the final targeted run.                                                                                                    |
| IR-V02 | validated | Reviewer owns in-editor appearance/usability validation and explicitly ended screenshot/matrix completion as an implementation gate. Temporary capture code is removed; no further capture work is required from the implementer. Automated layout/interaction checks remain required.                                                                  |
| IR-V03 | validated | The rebuilt SDK emits required dependency v2. Inspector and command validation now share the actual import pipeline's slot provider in the native fixture. All four source-backed/library-only glTF/FBX picker/history/reopen workflows pass. Schema validation remains strict, with expected/received version diagnostics; report unit tests pass 5/5. |

## 1. Goal and boundaries

Make the inspector understandable and maintainable without changing its authored
data, command semantics or established visual behavior. Reduce large files through
real component boundaries and reuse, not arbitrary file-length limits or additional
partial classes.

Primary source root: `projects/Oxygen.Editor.WorldEditor/src/Inspector/`.
Shared property controls: `projects/Oxygen.Editor.Controls/src/Inspector/`.
Existing numeric controls: `projects/Controls/InPlaceEdit/src/`.

Preserve:

- All current fields, enum choices, units, diagnostics and asset-reference intent.
- Scene/component selection, expansion, search/scope and diagnostic navigation.
- Layout, alignment, typography, disclosure, responsiveness and keyboard behavior.
- One undo entry per gesture; cancel restores captured targets; untouched axes stay
  unchanged; inspecting, searching and resizing never dirty the scene.
- Existing command ownership of validation, mutation, dirty state, persistence and
  live synchronization. Controls do not write components or call the engine.

Out of scope: docking redesign, new density preferences, new component types,
physical-camera exposure, component Ctrl-multiselect, new reset commands, engine
builds, schema changes and renderer work. The web proposal contains these ideas;
they are not automatically part of this refactor. Keep current atmosphere controls
available in their current locations; moving them exclusively to Scene Environment
requires a separate approved UX/contract change.

Requirements preserved: REQ-005 through REQ-009, REQ-022/024/026/037 and
SUCCESS-002/003 from the [Property Inspector LLD](../lld/property-inspector.md).
This plan protects existing coverage; it does not claim complete requirement delivery.

## 2. Read this before coding

Read in this order, focusing on the named sections rather than loading every design:

1. Root `AGENTS.md` and [Oxygen rules](../../oxygen/RULES.md).
2. [Inspector Numeric Editing](../ui/PROPERTY_EDITING.md): existing control and
   layout contracts, color conversion, ownership and established evidence.
3. [Property Inspector LLD](../lld/property-inspector.md), ownership and relevant
   field tables; [Property Pipeline LLD](../lld/property-pipeline.md), bindings,
   sessions and UI surfaces; [Environment Authoring LLD](../lld/environment-authoring.md).
4. Open <http://127.0.0.1:5178/>, choose **Design brief**, then read **Design intent**,
   **Component Inspector**, **Light inspectors**, **Scene Inspector**,
   **Property dependencies**, **WinUI & accessibility**, and **DroidNet control map**.
   Explore open/closed disclosures and narrow layouts in the workspace.
5. [MVVM overview](../../../projects/Mvvm/README.md), especially `IViewFor<T>` and
   `ViewModelChanged`; [VectorBox specification](../../../projects/Controls/InPlaceEdit/src/VectorBox/VectorBox.Spec.md).
6. [MSTest guidance](../../../.github/prompts/csharp-mstest.prompt.md) and
   [build/test workflows](../../../tooling/doc/build.md).

The web brief is presentation guidance, not proof of native implementation. Some
of its control-map descriptions are older than the current code: `PropertyCard`
already has responsive layout, `PropertiesExpander` already has `HeaderActions`,
and NumberBox already parses expressions. The current scene view has **42 cards**,
including the brief's four additions, not just 38. Do not implement these twice.
If the local brief is unavailable, ask the reviewer for access; do not invent its
missing requirements. Record conflicts before making changes.

### WinUI/XAML essentials for this task

| Mechanism                     | Use here                                                                    | Pitfall to avoid                                                                                                              |
| ----------------------------- | --------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| `UserControl`                 | A cohesive feature section or composed field with a known layout.           | Wrapping every label individually, or embedding another section card inside a card.                                           |
| `Control` / `ControlTemplate` | Existing reusable, themeable controls such as PropertyCard.                 | Copying their large templates into each feature view.                                                                         |
| Dependency property (DP)      | A control input that consumers must bind, style or update.                  | A plain CLR property does not participate in the XAML property system. DP defaults must not share mutable per-instance state. |
| `x:Bind`                      | Typed access to the view's ViewModel or a template's `x:DataType`.          | It defaults to OneTime and uses the view/template object, not automatically DataContext. Specify OneWay/TwoWay intentionally. |
| `{Binding}`                   | DataContext-based content, including template-parent bindings.              | Assuming WinUI supports WPF's FindAncestor binding; use explicit inputs/source or the existing view contract.                 |
| `Style` / resources           | Fonts, padding, alignment, minimum sizes and existing control defaults.     | Styles cannot encapsulate a repeated editor plus diagnostics composition by themselves.                                       |
| `ThemeResource`               | Brushes and other values that must follow theme changes.                    | Hardcoded dark-theme colors or globally overriding unrelated windows.                                                         |
| `Expander`                    | Native disclosure state/input with the existing quiet style.                | Clickable grids, custom pointer toggling, lost focus visuals or a second card border.                                         |
| `DataTemplate`                | Repeated asset/curve rows; declare `x:DataType` when the row type is known. | Using templates as arbitrary file includes for unique feature sections.                                                       |
| `ItemsRepeater`               | Existing repeated content with its existing realization lifecycle.          | Assuming it supplies ListView selection/keyboard behavior or automatic virtualization in an unbounded layout.                 |

Use DIPs, not physical pixels. DPI/rasterization scaling and accessible text scaling
are different inputs and must be checked separately. Use Auto rows, MinHeight and
padding for wrapping content; do not fix row heights or apply ScaleTransform to
simulate compactness. Preserve existing control instances during reflow so a drag
does not lose capture. Keep presentation objects such as brushes and curve points
in the view layer, not the view model.

Keep small code-behind adapters for control-specific edit events, focus, scrolling,
flyout dismissal and realization. Use Toolkit commands for semantic operations
(reset, inspect, clear, add/remove). Do not convert every event into a command or
introduce a behavior package merely to achieve zero code-behind.

## 3. Visual and interaction contract

The native UI tests and current native captures are the preservation baseline.
The design brief explains intent and helps evaluate improvements; a browser image
is not a pixel-exact native reference. Freeze a reproducible baseline in IR-01.

| Area               | Preserve and verify                                                                                                                                                                                                                                                                                    |
| ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Sections           | Atmosphere Lights, Sky Atmosphere, Background, Exposure, Tone Mapping, Color Grading, Bloom, in that order; current icon/title/description/header-action hierarchy.                                                                                                                                    |
| Disclosure         | Quiet full-width header; trailing right/down chevron; neutral expanded appearance; subtle hover/press; visible keyboard focus; static headings are not interactive. Primary/Secondary direction disclosures start closed. Preserve other current defaults, including Metering & limits initially open. |
| Scalar alignment   | Shared 124-DIP label column and 12-DIP gap at normal text size; scalar minimum editor width 128; input starts and suffix slots align. Scalar unit remains beside the number after stacking.                                                                                                            |
| Compound fields    | XYZ minimum editor width 248; RGB and multipliers use two rows with annotation in the header. Swatch only for colors. Equal channel widths, explicit X/Y/Z or R/G/B order, and vertical fallback without replacing inputs.                                                                             |
| Typography         | Existing 14-DIP numeric/body text and caption roles; labels/descriptions wrap. Preserve actual current per-role values rather than blindly applying prototype tokens to every TextBlock. Hover/drag does not change font metrics or row geometry.                                                      |
| Field presentation | Existing numeric mask, padding (including tested 6,4), corners, borders, label scrubbing, indeterminate state and automation names. Do not substitute WinUI NumberBox for DroidNet NumberBox.                                                                                                          |
| Search             | Match labels, original identifiers, descriptions and intentional keywords; reveal inactive matching values with applicability text; open matches; restore previous expansion after clearing. Scope changes retain stored fields. Display Gamma stays available with mapper None.                       |
| Host layout        | Compact node/component navigator, bounded component list and remaining dock height allocated to the property scroller. No new nested vertical scrollers.                                                                                                                                               |
| Errors/references  | Inline diagnostics with polite announcement, no duplicate label; disabled/None/missing states stay distinct; Inspect and Clear remain separate actions.                                                                                                                                                |

No unreviewed visual drift is accepted just because tests pass. A deviation may be
accepted if it improves usability **and** implementation quality. The implementer
must show before/after, explain the affected state, and obtain reviewer approval.
Update durable assertions only after that approval. Do not weaken tolerances,
delete assertions or substitute new screenshots to hide a regression. The known
Angular diameter degree/radian qualifier mismatch is a candidate correction, not
permission for unrelated relabeling.

## 4. Target structure and ownership

Proposed paths below are implementation destinations, not files already present.
Use a small number of cohesive folders; preserve externally referenced names until
all actual consumers can migrate in the same slice.

```text
WorldEditor/src/Inspector/
  SceneNodeEditorView[Model]                 # host orchestration, not field logic
  Environment/
    EnvironmentView[Model]                   # identity, lifetime, composition
    SkyAtmosphereSectionView[Model]
    AtmosphereLightsSectionView[Model]        # source roles and observation
    ExposureSectionView[Model]               # bounds, mode and exposure policy
    ExposureCompensationCurveEditor[Model]
    BackgroundSectionView[Model]
    PostProcessingSectionView[Model]         # smaller related effects
  Presentation/
    InspectorSearchModel                    # query/scope/expansion presentation
    InspectorFieldPresentation              # identity/keywords/applicability
  Controls/
    InspectorRgbField                       # current RGB helpers + composition
    AssetPickerContent                      # shared picker list presentation
  Editing/
    TransformEditController                 # transform-specific session policy
    InspectorEditSessionCoordinator          # existing common gesture machinery
```

Do not require a separate VM for a two-property section unless it has behavior to
own. PostProcessingSectionView may compose smaller views without separate VMs.
Directional Light may compose shadow subviews; its binding/selection owner remains
singular. Retain the existing `Geometry/` boundary.

- `Oxygen.Editor.Controls` owns reusable numeric/card layout composition. A proposed
  `InspectorNumberField` composes a **PropertyCard containing a NumberBox directly
  or inside its supported NumberBox/TextBlock panel**, not a UserControl hidden
  inside PropertyCard.Content. This preserves `UseEditorLabel` recognition.
- Shared controls accept values, mixed flags, text/diagnostic state and edit events;
  they do not reference WorldEditor diagnostics, scene commands or schema targets.
- WorldEditor owns field adapters, presentation metadata, color edit policy,
  selection and command routing. Keep RGB composition here initially because its
  existing helpers are feature-owned; move only genuinely reusable mechanics to
  an existing owner when necessary, not speculatively.
- Schemas owns the existing `PropertyBinding<T>` and canonical descriptors. Prefer
  a small WorldEditor adapter over changing this contract merely to reduce lines.
- Parent models create/own/dispose child models. Child views receive existing
  instances; they do not silently create replacement models or resolve services.
- Extracted views use the existing `[ViewModel]` / `IViewFor<T>` convention where
  applicable. On ViewModelChanged/Loaded/Unloaded, detach the previous observer,
  attach at most once to the current model, and settle gestures by existing policy.
  Unloaded is not permission to dispose a parent-owned model.

## 5. Implementation sequence

Each slice is reviewable and must leave a buildable, tested inspector. Work in the
listed order. Within each slice: characterize behavior, make one bounded change,
run focused tests, compare affected visuals, then request review. Do not postpone
visual comparisons until the final slice. No automatic commits.

### IR-01 — Baseline, inventory and test navigation

Dependencies: none. Projects/files: existing WorldEditor tests and testsupport;
this plan. No production changes.

1. Build/run current inspector UI tests using section 7. Record revision, dirty
   source state, SDK/configuration, test filter, pass/fail counts and blockers.
   Do not require a clean tree or overwrite concurrent edits.
2. Inventory each current field: stable property ID, displayed value/unit,
   conversion, section/disclosure, default expansion, diagnostic and edit owner.
   Include controls nested in atmosphere-source views and the compensation curve.
   Confirm 42 scene cards and seven section headers; do not equate cards to scalar
   property count (one RGB card has three channels).
3. Read `ResponsivePropertyRowsTests`, `ComponentLayoutTests`, `InspectorBindingTests`,
   `NumericGesturesTests`, `SelectionGesturesTests`, `DirectionalCompoundTests`,
   `BackgroundCardTests` and `AerialStartTests`. Use the coverage map in section 7.
4. Create local-only baseline captures for the matrix in section 6. Add focused
   **durable assertions** for any untested contract that this refactor will touch.
   Keep new capture scripts and extra capture calls ad hoc and uncommitted.
5. Locate test helpers using FindName, Tag or direct Content casts. Plan to replace
   only extraction-sensitive lookups with helpers that locate the actual section
   or field across child namescopes. Do not modify assertions yet just to fit the
   proposed tree.

Exit: reproducible baseline and inventory reviewed; existing failures separated
from regressions; no unexplained visual state missing from the matrix.

### IR-02 — Declare the real Environment layout in XAML

Dependencies: IR-01. Files: `EnvironmentView.xaml` and `.xaml.cs`; corresponding
search/disclosure tests.

1. Put sections in their current runtime order directly in XAML.
2. Declare Planet & ground, Scattering, Aerial perspective and the four Exposure
   disclosures directly around their existing cards. Copy existing defaults,
   spacing and QuietDisclosure style; do not restyle them.
3. Remove `OrganizeSceneSections` and `AddDisclosure`. Preserve card indexes needed
   by the existing filter with explicit references/registration while migrating;
   do not identify groups by display strings or reparent controls.
4. Verify 42 cards, exact headings/order, defaults, repeated open/close, search
   expansion/restoration, and no dirty/history changes. Compare section captures.

Exit: actual layout is visible in XAML; no runtime remove/reinsert or label-based
layout construction; all corresponding UI assertions and visual checks pass.

**IR-02 result — landed_needs_validation.** Runtime section reordering and disclosure creation
were removed. XAML declares the seven sections and nested groups in their existing
order/default state. Disclosure field identities are asserted in order; native UI
Automation repeatedly toggles each realized header and verifies the chevron and
content visibility. Debug/x64 WorldEditor and Unit.UI builds passed; focused
section/search/navigation tests passed 4/4 and the responsive/binding subset passed
57/57. Existing compound/disclosure captures were compared; Background section
captures show variable vertical clipping/displacement and the ad-hoc nested
capture attempts did not produce reliable complete content images. These are not
accepted deviations or full visual acceptance. The reviewer-requested Histogram
field order, ordered identity assertions, header automation and LF/XAML formatting
corrections are implemented. The temporary content-shape registration bridge was
removed in IR-03.

### IR-03 — Separate search/applicability from the control tree

Dependencies: IR-02. Files: `EnvironmentView.xaml.cs`, proposed `Presentation/`
types, Environment bindings, Unit and Unit.UI tests.

1. Add typed scope/category values and field presentation entries keyed by existing
   property identity plus a distinct presentation key where several UI fields edit
   one property. A vector's channel/gesture identity is not its display label.
2. Populate explicit label, identifier aliases, description and keywords from the
   IR-01 inventory. Search must not depend on a realized template, selected enum
   text or dynamically inserted diagnostic text. Preserve documented aliases; if
   removal of value-based searching affects a real consumer, seek approval first.
3. Move query token normalization, matching, applicability message and temporary
   expansion override/restoration into a small observable presentation model.
   Preserve current query splitting/AND matching before considering improvements.
4. Declare a help/applicability TextBlock in each affected field composition. Bind
   text/visibility instead of wrapping Content at runtime. Keep errors distinct
   from applicability notes.
5. Delete `CollectSearchTerms`, search-text caches over UI controls and dynamic note
   construction. Keep focus/BringIntoView in the view, using stable field identity.
6. Test multi-term/alias/no-match search, mode changes during search, scope changes,
   inactive Auto/Spot/Manual fields, None tone mapper, expansion restoration and
   diagnostic focus. Verify hidden controls retain authored values.

Exit: headless tests cover matching/applicability; native tests prove display and
focus behavior. Search no longer crawls or mutates the content tree.

**IR-03 result — landed_needs_validation.** Explicit metadata covers the 42 scene
cards and canonical scene descriptors. Matching retains punctuation/case-insensitive
AND tokens, original aliases and intentional enum/source keywords. Applicability
and expansion overrides/restoration are headless presentation policy; notes are
declared in XAML and bound separately from errors. Registration uses explicit
card/disclosure references, with no content-tree crawling or runtime wrapping.
Model replacement detaches the previous observer. Search unit tests pass 9/9;
binding/focus UI passes 36/36 and replacement/content retention passes 2/2.
Matched native visual acceptance remains outstanding.

### IR-04 — Pilot a reusable numeric field

Dependencies: IR-03. Projects: Editor.Controls, WorldEditor, WorldEditor tests.

1. Add the composed `InspectorNumberField` described in section 4. Expose DPs for
   label, qualifier/prefix, value, mixed state, mask, layout/minimum width, error
   text/error state and applicability text. Forward the existing NumberBox start,
   completion and validation event arguments without losing raw expression text.
2. Internally retain the supported PropertyCard/NumberBox/TextBlock relationship.
   Apply scoped shared styles to the internal card and number; explicitly forward
   FontSize and other control metadata where UserControl inheritance is insufficient.
   Do not add a duplicate static label or forward pointer drags from another label.
3. Migrate **Perspective Camera only**. Keep near/far related diagnostics and the
   existing edit coordinator. Update test lookup helpers for the new composition,
   not the expected alignment, error or gesture outcomes.
4. Run responsive metadata-detachment/rebinding and native caption-drag tests;
   resize during drag, unload/reload, test Enter/Escape and rejection/correction.
5. Only after pilot approval, migrate ordinary scalar fields in Environment and
   Directional Light in small batches. Leave special curve/vector editors explicit.

Exit: fewer repeated numeric/diagnostic blocks; same caption instance through
reflow, same measured positions/text sizes and same command/history semantics.
Stop and review if the wrapper needs new tree-search infrastructure or a copied
PropertyCard template. A necessary shared API change belongs to its owning control
and requires an approved, focused contract/test update first.

**IR-04 result — landed_needs_validation.** All 52 declared ordinary scalar fields
(Camera 4, Environment 29, Directional Light 16, atmosphere-source view 3) compose the existing
PropertyCard and NumberBox in shared `InspectorNumberField`, with DPs for field
metadata/value/mixed state/styles/feedback and original edit/validation arguments.
Command/session and related diagnostic ownership are unchanged. Specialized Aerial
Start validation/focus and curve/vector editing remain explicit. Scalar non-pointer
UI tests pass 49/49; the isolated native caption retry passes 7/7, including reflow
and reload. Six Light-theme reference/composed cases are pixel-identical at
260/340/480 DIPs with 100% text, 420 DIPs with 150%, and 480/760 DIPs with 200%.
Those references reconstruct the original direct card/number panel in the same
native fixture; they are not historical full-inspector screenshots. Full affected
inspector visual acceptance remains pending.

Combined verification: Debug/x64 shared Controls, WorldEditor, Unit, Unit.UI,
Integration.UI and editor app builds passed against installed-SDK references.
Full refactor suites passed **224/224 Unit** and **226/226 Unit.UI** before final
defect fixes. The final targeted native run passes **6/6**, covering both role
assignment policies and all four source-backed/library-only glTF/FBX workflows;
strict dependency-report unit tests pass **5/5**. Scoped inspector analysis passed.
Full suites were not rerun after the final fixes. Earlier 214/214, 212/212 and
221/221 results remain historical only. The reviewer owns in-editor validation.

### IR-05 — Extract RGB, curve and picker compositions

Dependencies: IR-04. Files: existing RGB/curve helpers, color view fragments,
`GeometryView.xaml`, Environment picker/curve fragments; relevant tests.

1. Extract `InspectorRgbField`: preserve PropertyCard.LeadingContent swatch,
   stacked annotation, configured VectorBox, per-channel events, diagnostics and
   automation names. Provide an explicit color/multiplier distinction; multipliers
   never have a gamut-limited picker. Reuse `InspectorRgbPresentation` and
   `InspectorColorGestures`, including captured EditScopeId protection.
2. Migrate Background first, then Ground Albedo and light color; compare each before
   proceeding. Keep per-axis edits target-specific; a full picker edit intentionally
   changes the complete RGB value. Merely displaying a swatch must not author data.
3. Extract ExposureCompensationCurveEditor and its key/list policy. Retain the
   160-by-36 preview, current key limit, finite/increasing-EV validation, add/remove,
   edit grouping, diagnostics and undo. Keep projected Points in the view helper.
4. Extract duplicated geometry/material picker content as shared presentation with
   typed row templates. Keep catalog/material services, stable rows and assignment
   target capture in Geometry's owning model. Texture picker can reuse the content
   only if its row contract genuinely matches; do not build a universal picker API.
5. Preserve current picker control type, opening/dismissal, focus and layout. A
   switch to ListView may be worthwhile, but is a separate reviewed deviation with
   keyboard/selection tests, not a prerequisite for extraction.

Exit: RGB values/swatches, curve preview and reference assignment behave identically;
all affected matrix captures compared. No scene/service dependency enters shared
Editor.Controls and no async picker completion edits a later selection.

**IR-05 result — landed_needs_validation.** RGB colors and multipliers now share the existing
card/vector composition and captured-owner picker policy. Background, Ground Albedo,
Directional Light color, Sky Luminance and both disk-scale presentations are migrated.
The curve editor owns stable key rows and their policy, borrows the existing scene
edit owner and diagnostics, and preserves view-owned preview points. Geometry/material
flyouts use shared picker content with typed row templates; services and assignment
target capture remain with Geometry. The texture layout is intentionally not forced
into the different geometry/material row contract. RGB focused UI passes 53/53;
curve/search unit tests pass 17/17 and Inspector binding UI passes 36/36.
Fourteen new composition/adapter cases pass; typed picker invocations retain the
original row, and accessory brush/corner/automation changes update without replacing
inputs. Ten color/multiplier image pairs are pixel-identical across 100/150/200%
font-size cases. Geometry/material list pairs are pixel-identical, including
notices, None, queued and disabled/missing rows. The curve preview/key composition
matches the original layout and typography; only 46 rounded-border alpha pixels
differ by at most 3/255, with no RGB, geometry or text differences. All comparisons
use Light native fixtures and source-reconstructed direct compositions, not historical
full-inspector captures. Temporary capture edits are removed. Full affected section
and native popup/focus visual checks remain under IR-V02.

### IR-06 — Extract Environment sections and their models

Dependencies: IR-03 through IR-05. Files: Environment views/models/observation,
AtmosphereSourceEditor, view registration/composition, testsupport consumers.

1. Extract one section at a time: Background, Sky Atmosphere, Exposure, Atmosphere
   Lights, then the smaller post-processing composition. Supply typed child models
   through the existing view convention. Carry the shared styles into the correct
   resource scope and verify URI/resource resolution after moving XAML.
2. Move each section's properties, diagnostics, display-unit adapters and change
   submission with it. Parent retains scene identity/lifetime, selection and shared
   document context. Do not maintain forwarding properties on the parent just for
   old tests; migrate actual consumers and tests in the same bounded change.
3. Give scene-environment edits one explicit shared edit/diagnostic owner, injected
   into the child models. Move light-assignment policy/observation and its existing
   coordinator into AtmosphereLightsSectionViewModel. Do not create competing
   histories or one new transaction service per child. Child models do not dispose
   a borrowed coordinator.
4. Preserve paired exposure bounds/percentile diagnostics, current initial-source
   reset snapshots, primary/secondary DirectionalLight model instances and source
   role conflict behavior. Bind selection/context before accepting child input.
5. Move Aerial Start focus realization into the Sky section; parent asks by stable
   ID. Clear query/scope, expand the group, wait for realization, scroll/focus and
   acknowledge only on success. Do not FindName across UserControl namescopes.
6. Handle model replacement while loaded as well as unload/reload. Remove old
   observers before adding new ones; preserve injected scheduler behavior and
   cancellation of queued asset callbacks. Test both source and scene replacement.

Exit: EnvironmentView is composition rather than a field catalog; no parent access
to child internals to construct layout; 42 cards remain discoverable through test
helpers; search/reset/diagnostic navigation, scheduling and source dependencies pass.

**IR-06 result — validated implementation gates.** The parent owns scene identity and five
stable section models/views; ordinary scene edits share one explicit edit/diagnostic
owner. Source role policy, subtree/component observation, initial snapshots and both
canonical Light editors belong to Atmosphere Lights. Exposure owns the scheduled
texture catalog and curve model. Each view registers its own controls; the parent
uses the registry for browsing, not child namescopes to build layout. Sky owns
Aerial Start realization/focus. All consumers and tests now access section-owned
properties, with no parent property-forwarding compatibility layer. Final visual
and native checks are recorded in IR-09.

### IR-07 — Reduce scalar binding duplication and split Light presentation

Dependencies: IR-06. Files: PerspectiveCameraViewModel, DirectionalLightViewModel,
their presentation/views, existing schema binding consumers.

1. Pilot on one camera scalar: bind to a public typed PropertyBinding surface or a
   small field adapter exposing Value, IsMixed and diagnostics. Remove the duplicate
   observable value/mixed fields and their manual synchronization only when model
   refresh and rejected-value restoration are proven equivalent.
2. Use a generic, disposable registration helper for repeated ValueRequested
   wiring. It must enforce input-enabled checks, ModelChanged diagnostic semantics,
   target/context lifetime and detach subscriptions. No reflection, dynamic types,
   global registry or new abstract-editor hierarchy.
3. Keep explicit adapters for kilometres/metres and degrees/radians; keep vector
   per-axis semantics and sun-direction policy explicit. Do not bind full Vector3
   replacement to a mixed-axis edit. Use NotifyPropertyChangedFor only for simple
   computed-property dependencies, not command submission side effects.
4. Extract Light shadow/cascade presentation subviews if still substantial after
   primitive reuse. Supply the same owning Light model; do not reconstruct it when
   a disclosure opens. Move pure orientation conversion into a focused existing
   owner or local collaborator, preserving Z-up, emitted forward -Y and parent rules.
5. Present the Angular diameter qualifier correction (rad to degrees) separately
   for review; verify stored/native values are unchanged and conversion occurs once.

Exit: ordinary field state has one source; less repeated synchronization/wiring;
near/far, mixed fields, shadows, sun direction and color gestures retain behavior.
No extension of arithmetic semantics to previously unsupported fields by accident.

**IR-07 result — validated implementation gates.** Camera's four public typed bindings own
Value/IsMixed directly. Generic disposable registrations route value requests and
suppress model-refresh echoes, preserving rejection diagnostics and cancellation.
Light's ordinary scalar adapters use their existing bindings as the sole state;
per-target vector edits and degree/radian/count conversions remain explicit.
Shadow/cascade composition receives the existing Light model, while pure orientation
conversion has a focused owner. The existing `rad` qualifier is intentionally
unchanged until its separate visual correction is reviewed; stored values and the
degree adapter remain unchanged. Final qualification is recorded in IR-09.

### IR-08 — Extract Transform sessions and inspector-host collaborators

Dependencies: IR-07. Files: TransformViewModel, InspectorEditSessionCoordinator,
SceneNodeEditorViewModel and relevant partials; test fixtures/composition.

1. Move Transform's session dictionaries, wheel idle work, gate/pending task lifetime,
   relative expression policy and cancellation into TransformEditController. Supply
   captured context/selection and existing command operations. Keep field values
   and mixed/diagnostic presentation in the VM.
2. Characterize each terminal path before extraction. Preserve per-target original
   values, late-event suppression, single terminal submission, pending task draining
   and safe disposal. Do not make the common coordinator handle relative expressions
   by adding boolean policy combinations. Share matching mechanics only after tests
   establish equivalence and a reviewer accepts the boundary.
3. Extract editor factory construction from SceneNodeEditorViewModel into an
   inspector-local factory collaborator. Reuse scoped dependencies; no second DI
   container and no global service locator. Preserve cached editor instances.
4. Extract selected component/subtree observation into a disposable collaborator
   with one explicit callback. Keep component filter transition, selected scene,
   editor enablement and navigation with the host. Leave view realization, draft
   settlement before hiding and scroll-to-top in SceneNodeEditorView.
5. Test add/remove/replacement, filter switching, pending drag/text, async picker,
   queued wheel work, document switch, deletion and unload/reload. Inspect still
   navigates without changing viewport or dirty state.

Exit: Transform VM no longer owns async session machinery; inspector host no longer
constructs every editor or owns every subscription. Gesture and layout suites pass,
including resizing/reflow while an edit is active.

**IR-08 result — validated implementation gates.** Transform's controller owns sessions,
original target values, relative expressions, wheel timers, the edit gate, pending
draining and disposal. The VM retains values/mixed/source feedback. Inspector-local
factory construction reuses scoped services and the host's cached instances. A
disposable selected-component/material-slot observer has one typed callback and
generation-guards queued work; host navigation/filter/input orchestration stays
with the host. Final lifetime/native qualification is recorded in IR-09.

### IR-09 — Retire duplicates and qualify the result

Dependencies: all previous slices. Files: WorldEditor local PropertyCard/
PropertiesExpander code/templates, `ThemeResources.xaml`, `Themes/Generic.xaml`,
owning documentation and tests; shared resource consumers as required.

1. Search the entire repository for old fully qualified types, XAML namespaces,
   resource URIs, style keys and test consumers. Only after proving migration,
   retire duplicate local controls/templates and their Generic.xaml references.
   Do not delete the live shared versions or unrelated resource aliases.
2. Review new files for clear ownership, explicit inputs, correct member style,
   observer/disposal symmetry and absence of compatibility forwarding scaffolding.
   No hard line limit: explain any large cohesive algorithm; reject cosmetic partials.
3. Build the editor and owning tests; run full relevant Unit and Unit.UI suites,
   targeted Integration.UI workflows and opt-in C# analysis. A missing installed
   native SDK is a blocker, not authorization for an engine build.
4. Repeat the full visual matrix, including live native editor inspection. Obtain
   approval for deviations; document compact results rather than committing images.
5. Update owning UI/LLD documentation only where the architecture actually changed.
   Record final outcome/remaining/checks beside this plan. Link a concise summary
   from IMPLEMENTATION_STATUS.md without rewriting historical milestone evidence
   or claiming missing renderer/cooker workflows are now qualified.

Exit: no duplicate control authority, no unexplained visual deviations, no regression
in the covered authoring workflows, all required evidence recorded accurately.

**IR-09 result — implementation complete; reviewer validation pending.** Repository-wide searches found no live consumer of
the old local PropertyCard/PropertiesExpander types or resource URIs outside their
own templates and Generic references. The unused implementations/templates and
local theme dictionary are removed; Generic now references the shared owner and
retains ViewportIcons. The coordinated implementation passes the full Unit suite
224/224 and full Unit.UI 226/226 before the final defect fixes. The final targeted
native run passes 6/6 and strict report tests pass 5/5. Scoped analysis found no
reported diagnostics in the modified inspector production files. No temporary
capture code remains. The reviewer owns in-editor validation; no broader final
suite rerun or manual acceptance is claimed.

## 6. Reviewer-owned in-editor validation

The reviewer explicitly ended screenshot/matrix completion as an implementation
gate. Stop capture work and deliver the implemented editor for the reviewer's own
in-editor validation. This supersedes earlier per-slice capture and full-matrix
requirements; it does not authorize ignoring defects or weakening behavioral tests.

Implementation qualification retains automated geometry, typography, disclosure,
search, focus, interaction, mixed/per-axis, undo/cancel, persistence and native
synchronization checks. Fix failures and report exact causes. Manual appearance,
OS accessibility/theme and live-editor usability acceptance belong to the reviewer.
Do not claim those manual checks have been performed on the reviewer's behalf.

Temporary capture code is removed, and any existing images/reference reconstructions
remain outside Git. Reconstructed references with binding/realization differences
are not trustworthy historical parity evidence and are not used to close defects.

## 7. Verification map and commands

Run from repository-root PowerShell in a Visual Studio developer shell. Provision
the pinned environment with `./init.ps1 -NoRestore` when needed; see root AGENTS.md.
Use the installed Debug native SDK at `projects/Oxygen.Engine/out/install/Debug`.
Do not build CMake or run engine provisioning as part of this UI refactor. Avoid
concurrent MSBuild runs sharing outputs.

Example scoped build/run pairs (the runner does not build):

```powershell
MSBuild.exe projects/Oxygen.Editor.WorldEditor/tests/Unit.UI/Oxygen.Editor.WorldEditor.Unit.UI.Tests.csproj /restore /m /p:Configuration=Debug /p:Platform=x64
traverse Invoke-Tests --start projects/Oxygen.Editor.WorldEditor/tests/Unit.UI --configuration Debug -- --filter FullyQualifiedName~Inspector

MSBuild.exe projects/Oxygen.Editor.WorldEditor/tests/Unit/Oxygen.Editor.WorldEditor.Unit.Tests.csproj /restore /m /p:Configuration=Debug /p:Platform=x64
traverse Invoke-Tests --start projects/Oxygen.Editor.WorldEditor/tests/Unit --configuration Debug

MSBuild.exe projects/Oxygen.Editor.WorldEditor/tests/Integration.UI/Oxygen.Editor.WorldEditor.Integration.UI.Tests.csproj /restore /m /p:Configuration=Debug /p:Platform=x64
traverse Invoke-Tests --start projects/Oxygen.Editor.WorldEditor/tests/Integration.UI --configuration Debug -- --filter FullyQualifiedName~Inspector

MSBuild.exe projects/Oxygen.Editor/src/Oxygen.Editor.App.csproj /restore /m /p:Configuration=Debug /p:Platform=x64
MSBuild.exe projects/Oxygen.Editor.WorldEditor/src/Oxygen.Editor.WorldEditor.csproj /m /p:Configuration=Debug /p:Platform=x64 /p:RunAnalyzersDuringBuild=true
```

For a single slice replace the filter with the relevant class/scenario. At IR-09
run the entire built Unit.UI suite by omitting the filter. Include separate shared
control builds/analysis and affected consumers (for example MaterialEditor) if a
shared API/resource was changed. Use existing WorldEditor UI tests for the composed
control initially; do not assume an Editor.Controls test project exists.

| Concern                                               | Existing tests to read/run; add focused cases rather than replacing them                                                                                                                                                |
| ----------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Alignment, qualifiers, typography, reflow, disclosure | Unit.UI/Inspector/ResponsivePropertyRowsTests.cs; InspectorBindingTests.cs; ComponentLayoutTests.cs.                                                                                                                    |
| Search/defaults/card inventory                        | InspectorBindingTests.ScenePropertySearchRevealsInactiveAutoFieldsAndScopeFiltersSections; SceneSectionsPreserveCardsAndRestoreNestedDisclosureAfterSearch.                                                             |
| Native interaction and lifetimes                      | NumericGesturesTests.cs, SelectionGesturesTests.cs, ComponentEditBoundariesTests.cs, ComponentFiltersTests.cs.                                                                                                          |
| RGB, curve, history                                   | BackgroundCardTests.cs, DirectionalCompoundTests.cs; Unit/Inspector/ColorHistoryTests.cs. BackgroundCardTests contributes methods to partial InspectorBindingTests: filter by class or actual method, not its filename. |
| Diagnostics and focus                                 | DiagnosticsTests.cs, AerialStartTests.cs, ComponentFeedbackTests.cs; Unit/Inspector/InspectorFieldDiagnosticsTests.cs.                                                                                                  |
| Scene/source observation                              | SunDependenciesTests.cs; Unit/Inspector/EnvironmentSchedulingTests.cs; Integration.UI/Inspector/AtmosphereLightAssignmentTests.cs and ComponentLifetimeTests.cs.                                                        |
| Asset choices and material slots                      | GeometryStatusTests.cs, MaterialStatusTests.cs, MaterialSlotsTests.cs, BuiltinCatalogTests.cs, AssetStatusTests.cs, ContentDemandTests.cs, ImportedAssetsTests.cs.                                                      |
| Save/reopen/live synchronization                      | Integration.UI/Inspector/ComponentFieldSynchronizationTests.cs, RotationPersistenceTests.cs, EnvironmentSynchronizationTests.cs, GeometrySynchronizationTests.cs, MaterialSlotSynchronizationTests.cs.                  |

Tests use MSTest, AwesomeAssertions and the shared unpackaged WinUI host. Follow
VisualUserInterfaceTests.EnqueueAsync/LoadTestContentAsync and existing fixture
disposal. Use render waits before reading bounds and native/automation input for
interaction assertions where appropriate. Do not assert a private helper call
instead of the visible result.

Extraction changes XAML namescopes: root.FindName does not locate names inside a
child UserControl. Update `testsupport/UI/InspectorControls.cs` and local helpers
to locate the real section by stable identity/automation metadata and then query
its own namescope. Preserve meaningful template-part assertions at the shared
control level. Do not add public forwarding properties, duplicate invisible
controls or recursive production FindName workarounds solely to keep tests unchanged.

For changed files run scoped hooks and `git diff --check`; review compiler/XAML
warnings and opt-in analysis. Hooks do not replace C# analysis. No native screenshot
review or tests are required to _write_ this plan; they are mandatory to qualify
its implementation.

## 8. Review gates, risks and progress

Reviewer means the user or the designated maintainer; no specific individual is
assigned by this plan. Implementer is the developer executing a slice.

| Risk/decision                                                                        | Owner and next action                                                                                               |
| ------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------- |
| Wrapping breaks scalar native caption, shared column measurement or resource lookup. | Implementer pilots IR-04; reviewer approves boundary before broad migration.                                        |
| Extracted views accidentally create/dispose models or retain old observers.          | Implementer tests replacement/reload in IR-06; reviewer checks ownership explicitly.                                |
| Search metadata loses old field names or mode-specific discoverability.              | Implementer maps all IR-01 inventory entries to IR-03 metadata and asserts coverage.                                |
| Generic bindings erase per-axis/relative/mixed semantics.                            | Implementer retains special adapters; reviewer approves IR-07/08 only after gesture evidence.                       |
| Prototype conflicts with current native tests/accepted LLD.                          | Reviewer decides before an intentional change; implementer records proposed deviation, not an assumed new contract. |
| Old local controls still have resource/test consumers.                               | Implementer inventories all consumers in IR-09; no deletion until migration is proven.                              |
| Screenshots look correct but behavior regressed, or tests pass but pixels drifted.   | Implementer supplies both test results and comparisons per slice; reviewer accepts both.                            |
| Native SDK/test runner unavailable or existing suite failing.                        | Implementer records the exact blocker and unrun checks; reviewer resolves tooling/verification ownership.           |

| ID    | State                   | Next action                                                                                                                           | Responsible role |
| ----- | ----------------------- | ------------------------------------------------------------------------------------------------------------------------------------- | ---------------- |
| IR-01 | validated               | Inventory and behavioral baseline recorded; capture requirement superseded by reviewer-owned validation.                              | Implementer      |
| IR-02 | validated               | Declarative layout, ordering and disclosure checks complete.                                                                          | Implementer      |
| IR-03 | validated               | Headless browsing, applicability, focus and replacement lifetime checks complete.                                                     | Implementer      |
| IR-04 | validated               | All 52 scalar compositions and gesture/row contracts implemented and checked.                                                         | Implementer      |
| IR-05 | validated               | RGB/curve/picker ownership and adapter checks complete.                                                                               | Implementer      |
| IR-06 | validated               | Five typed sections and shared scene edit ownership implemented; automated checks passed.                                             | Implementer      |
| IR-07 | validated               | Scalar state consolidated; Light shadow/orientation owners extracted. Existing angular qualifier retained for reviewer consideration. | Implementer      |
| IR-08 | validated               | Transform controller and factory/selection observer implemented; gesture/lifetime checks passed.                                      | Implementer      |
| IR-09 | landed_needs_validation | Implementation/audit/defect fixes complete. Reviewer performs final in-editor validation; no further capture work.                    | Reviewer         |

Use `in_progress`, `landed_needs_validation`, `blocked` and `validated` accurately.
IR-06–09 are a coordinated implementation batch at the reviewer's request; do not
pause at each extraction substep. Retain the ownership and acceptance gates, and
record each slice's actual result separately. After each slice record only outcome, remaining work, test commands/results,
reviewed visual states and approved deviations beside that slice. Do not add a
session diary or claim validation from compilation alone.

### IR-01 baseline and field inventory

Baseline revision: `77b87e3926c4d9e4dc1f903561786813bbfe3e5f`. The initial working
tree had a user-edited plan index and this untracked plan; no Inspector production
files were changed. Windows Debug/x64 used .NET SDK `10.0.401`, Visual Studio
MSBuild `18.10.1`, and the installed Debug native SDK. The baseline build completed
with three transient MSB3026 file-lock retries while other MSBuild processes held
shared outputs; the Inspector UI run passed **155/155** tests with
`FullyQualifiedName~Inspector`. Existing `InspectorCapture` calls saved local-only
native WinUI captures for responsive scalar/compound fields, Background, Sky
Luminance and Light/Dark expanded disclosures. Representative before/after pairs
were visually compared; the Background row capture shows a small editor-row
vertical displacement that remains under investigation and is not accepted as a
deviation. No native editor-window capture was made for this baseline slice.

The current Environment view declares 42 outer `PropertyCard` instances. A card
may represent several channel editors; it is not a scalar-property count. `E` is
the `EnvironmentViewModel` scene-property coordinator and its canonical scene
descriptor/diagnostic owner; `L` is the owning `DirectionalLightViewModel` and
light-property coordinator; `A` is the scene light-assignment coordinator. The
document command service remains the authority for validation, mutation, dirty
state, history, persistence and live synchronization. A dash means the XAML has
no dedicated inline diagnostic for that field.

| Section / current group             | Card and editor(s)             | Stable source identity; display, conversion and current value                                                                                                                                                                                                                   | Feedback / edit owner                                                     |
| ----------------------------------- | ------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| Sky Atmosphere / common             | Enabled                        | `SceneEnvironment:/atmosphere_enabled`; bool; `true`                                                                                                                                                                                                                            | E; no inline message                                                      |
| Sky Atmosphere / common             | Sun Disk                       | `SceneEnvironment:/sky_atmosphere/sun_disk_enabled`; bool; `true`                                                                                                                                                                                                               | E                                                                         |
| Sky Atmosphere / Planet & ground    | Planet Radius                  | `SceneEnvironment:/sky_atmosphere/planet_radius_meters`; `PlanetRadiusKm`, km display (`m / 1000`); `6360 km`                                                                                                                                                                   | E                                                                         |
| Sky Atmosphere / Planet & ground    | Atmosphere Height              | `SceneEnvironment:/sky_atmosphere/atmosphere_height_meters`; `AtmosphereHeightKm`, km display (`m / 1000`); `100 km`                                                                                                                                                            | E                                                                         |
| Sky Atmosphere / Planet & ground    | Ground Albedo                  | `SceneEnvironment:/sky_atmosphere/ground_albedo_rgb`; linear RGB channels, display-converted swatch only; `(0.4, 0.4, 0.4)`                                                                                                                                                     | E, shared vector diagnostic; picker edit uses RGB gesture owner           |
| Sky Atmosphere / Scattering         | Rayleigh Height                | `SceneEnvironment:/sky_atmosphere/rayleigh_scale_height_meters`; `RayleighScaleHeightKm`, km display (`m / 1000`); `8 km`                                                                                                                                                       | E                                                                         |
| Sky Atmosphere / Scattering         | Mie Height                     | `SceneEnvironment:/sky_atmosphere/mie_scale_height_meters`; `MieScaleHeightKm`, km display (`m / 1000`); `1.2 km`                                                                                                                                                               | E                                                                         |
| Sky Atmosphere / Scattering         | Mie Anisotropy                 | `SceneEnvironment:/sky_atmosphere/mie_anisotropy`; dimensionless; `0.8`                                                                                                                                                                                                         | E                                                                         |
| Sky Atmosphere / common             | Sky Luminance                  | `SceneEnvironment:/sky_atmosphere/sky_luminance_factor_rgb`; linear RGB multipliers; `(1, 1, 1)`; no color picker                                                                                                                                                               | E, shared vector diagnostic                                               |
| Sky Atmosphere / Aerial perspective | Distance scale                 | `SceneEnvironment:/sky_atmosphere/aerial_perspective_distance_scale`; multiplier; `1`                                                                                                                                                                                           | E                                                                         |
| Sky Atmosphere / Aerial perspective | Scattering strength            | `SceneEnvironment:/sky_atmosphere/aerial_scattering_strength`; multiplier; `1`                                                                                                                                                                                                  | E                                                                         |
| Sky Atmosphere / Aerial perspective | Start distance                 | `SceneEnvironment:/sky_atmosphere/aerial_perspective_start_depth_meters`; metres; `100 m`                                                                                                                                                                                       | E; field-specific nonnegative validation                                  |
| Sky Atmosphere / Aerial perspective | Height fog contribution        | `SceneEnvironment:/sky_atmosphere/height_fog_contribution`; multiplier; `1`                                                                                                                                                                                                     | E                                                                         |
| Atmosphere Lights                   | Sources                        | Composite: per-light `DirectionalLight:/atmosphere_light_slot`, `/angular_size_radians`, `/atmosphere_disk_luminance_scale_rgb`, `/use_per_pixel_atmosphere_transmittance`, plus Transform rotation axes for derived direction; Primary/Secondary selectors; slots start `None` | A for assignment conflicts; L for each bound source                       |
| Exposure / common                   | Enabled                        | `SceneEnvironment:/post_process/exposure_enabled`; bool; `true`                                                                                                                                                                                                                 | E                                                                         |
| Exposure / common                   | Mode                           | `SceneEnvironment:/post_process/exposure_mode`; Manual / ManualCamera / Auto; current DTO default `Manual`                                                                                                                                                                      | E                                                                         |
| Exposure / common                   | Manual EV                      | `SceneEnvironment:/post_process/manual_exposure_ev`; EV100; `13`; Manual only                                                                                                                                                                                                   | E; paired with the mode applicability note                                |
| Exposure / common                   | Compensation                   | `SceneEnvironment:/post_process/exposure_compensation_ev`; EV; `0`                                                                                                                                                                                                              | E                                                                         |
| Exposure / Histogram & calibration  | Key                            | `SceneEnvironment:/post_process/exposure_key`; calibration scale; current DTO default `12.5`                                                                                                                                                                                    | E                                                                         |
| Exposure / Metering & limits        | Metering                       | `SceneEnvironment:/post_process/auto_exposure_metering_mode`; Average / CenterWeighted / Spot; `Average`; Auto only                                                                                                                                                             | E                                                                         |
| Exposure / Exposure shaping         | Metering mask                  | `SceneEnvironment:/post_process/auto_exposure_metering_mask`; nullable texture descriptor URI; `None`; Auto only                                                                                                                                                                | E; asset rows and assignment live in Environment view/model               |
| Exposure / Metering & limits        | Auto Min EV                    | `SceneEnvironment:/post_process/auto_exposure_min_ev`; EV100; `-6`; Auto only                                                                                                                                                                                                   | E; paired min/max diagnostic                                              |
| Exposure / Metering & limits        | Auto Max EV                    | `SceneEnvironment:/post_process/auto_exposure_max_ev`; EV100; `16`; Auto only                                                                                                                                                                                                   | E; paired min/max diagnostic                                              |
| Exposure / Adaptation               | Adapt Up                       | `SceneEnvironment:/post_process/auto_exposure_speed_up`; EV/s; `3`; Auto only                                                                                                                                                                                                   | E                                                                         |
| Exposure / Adaptation               | Adapt Down                     | `SceneEnvironment:/post_process/auto_exposure_speed_down`; EV/s; `1`; Auto only                                                                                                                                                                                                 | E                                                                         |
| Exposure / Adaptation               | Adaptation transition distance | `SceneEnvironment:/post_process/auto_exposure_transition_distance_ev`; EV; `1.5`; Auto only                                                                                                                                                                                     | E                                                                         |
| Exposure / Histogram & calibration  | Low Percentile                 | `SceneEnvironment:/post_process/auto_exposure_low_percentile`; fraction; `0.1`; Auto only                                                                                                                                                                                       | E; paired percentile diagnostic                                           |
| Exposure / Histogram & calibration  | High Percentile                | `SceneEnvironment:/post_process/auto_exposure_high_percentile`; fraction; `0.9`; Auto only                                                                                                                                                                                      | E; paired percentile diagnostic                                           |
| Exposure / Histogram & calibration  | Min Log Luminance              | `SceneEnvironment:/post_process/auto_exposure_min_log_luminance`; log2 luminance; `-12`; Auto only                                                                                                                                                                              | E                                                                         |
| Exposure / Histogram & calibration  | Log Luminance Range            | `SceneEnvironment:/post_process/auto_exposure_log_luminance_range`; log2 span; `25`; Auto only                                                                                                                                                                                  | E                                                                         |
| Exposure / Histogram & calibration  | Dark-sample influence          | `SceneEnvironment:/post_process/auto_exposure_black_influence`; fraction; `0`; Auto only                                                                                                                                                                                        | E                                                                         |
| Exposure / Metering & limits        | Target Luminance               | `SceneEnvironment:/post_process/auto_exposure_target_luminance`; linear target; `0.18`; Auto only                                                                                                                                                                               | E                                                                         |
| Exposure / Metering & limits        | Spot Radius                    | `SceneEnvironment:/post_process/auto_exposure_spot_meter_radius`; normalized image radius; `0.2`; Auto + Spot only                                                                                                                                                              | E                                                                         |
| Exposure / Exposure shaping         | Exposure-compensation curve    | `SceneEnvironment:/post_process/auto_exposure_compensation_curve`; ordered EV100/EV pairs; empty by default; 64-key maximum                                                                                                                                                     | E; one curve diagnostic; list edits use the existing coordinator          |
| Tone Mapping                        | Mapper                         | `SceneEnvironment:/post_process/tone_mapper`; AcesFitted / Filmic / Reinhard / None; `AcesFitted`                                                                                                                                                                               | E; no dedicated inline message                                            |
| Tone Mapping                        | Display Gamma                  | `SceneEnvironment:/post_process/display_gamma`; dimensionless; `2.2`; remains available for mapper None                                                                                                                                                                         | E                                                                         |
| Bloom                               | Intensity                      | `SceneEnvironment:/post_process/bloom_intensity`; multiplier; `0`                                                                                                                                                                                                               | E                                                                         |
| Bloom                               | Threshold                      | `SceneEnvironment:/post_process/bloom_threshold`; linear HDR; `1`                                                                                                                                                                                                               | E                                                                         |
| Color Grading                       | Saturation                     | `SceneEnvironment:/post_process/saturation`; multiplier; `1`; hidden when Tone Mapper is None except during matching search                                                                                                                                                     | E                                                                         |
| Color Grading                       | Contrast                       | `SceneEnvironment:/post_process/contrast`; multiplier; `1`; same applicability                                                                                                                                                                                                  | E                                                                         |
| Color Grading                       | Vignette                       | `SceneEnvironment:/post_process/vignette_intensity`; fraction; `0`; same applicability                                                                                                                                                                                          | E                                                                         |
| Background                          | Color                          | `SceneEnvironment:/background_color`; linear SDR RGB channels; `(0, 0, 0)`; swatch picker converts display sRGB to linear once                                                                                                                                                  | E, shared vector diagnostic; picker uses the existing color gesture owner |

The single Sources card contains two independent source disclosures, both closed
by default. Each `AtmosphereSourceView` has five additional cards: Azimuth and
Elevation are world-direction projections of node orientation shown in degrees;
Angular diameter edits the light's `/angular_size_radians` through the degree
adapter; Disk luminance scale edits `/atmosphere_disk_luminance_scale_rgb` as a
multiplier; Per-pixel transmittance edits
`/use_per_pixel_atmosphere_transmittance`. These child controls share the primary
or secondary `DirectionalLightViewModel`, diagnostics and light edit owner; they
are not added to the 42 outer scene-card count. The compensation-curve card has a
160-by-36 view-projected preview and repeated `Metered EV100` / `Compensation EV`
NumberBoxes; both edit one canonical curve property through the existing grouped
edit path.

Other component fields in the same inspector host are:

| Editor             | Stable identities and current presentation                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   | Owner                                                                                                                                                          |
| ------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Transform          | Position X/Y/Z: `Transform:/local_position/0..2`, metres. Rotation X/Y/Z: `Transform:/local_rotation_euler_degrees/0..2`, degrees projected from XYZW quaternion. Scale X/Y/Z: `Transform:/local_scale/0..2`, multipliers. Defaults are `(0,0,0)`, identity, `(1,1,1)`; mixed state and diagnostics are per axis.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | TransformViewModel and its Transform-specific gesture/session machinery; document property commands own mutation/history.                                      |
| Perspective Camera | Field of view `/field_of_view_degrees` (60°), Aspect ratio `/aspect_ratio` (16:9), Near plane `/near_plane` (0.1 m), Far plane `/far_plane` (1000 m). FOV is stored in degrees and converted at the native boundary.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         | PerspectiveCameraViewModel/PropertyBinding and the shared edit-session coordinator; near/far diagnostics are related.                                          |
| Directional Light  | Color `/color` (linear RGB); Illuminance `/intensity_lux` (100000 lux); Angular diameter `/angular_size_radians` (stored radians; this view binds degree presentation while its current qualifier says `rad`); Azimuth/Elevation are derived orientation; Source assignment `/atmosphere_light_slot`; Per-pixel transmittance `/use_per_pixel_atmosphere_transmittance`; Disk scale `/atmosphere_disk_luminance_scale_rgb`; Affects world `/affects_world`; Exposure `/exposure_compensation_ev`; shadows `/casts_shadows`, `/shadow/contact_shadows`, `/shadow/resolution_hint`, `/shadow/bias`, `/shadow/normal_bias`; CSM `/csm/split_mode`, `/csm/cascade_count`, `/csm/max_shadow_distance`, `/csm/distribution_exponent`, `/csm/cascade_distances/0..3`, `/csm/transition_fraction`, `/csm/distance_fadeout_fraction`. | DirectionalLightViewModel bindings and its edit coordinator; direction is target-specific Transform data. Current angular qualifier is a review item in IR-07. |
| Geometry           | Asset assignment `Geometry:/geometry_uri`; dynamic material slot identity (`MaterialSlotId` + observed geometry/revision) and material reference are specialized asset commands rather than scalar property descriptors.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     | GeometryViewModel owns catalog/material services, stable picker rows and captured assignment target.                                                           |

Field descriptors in `SceneEnvironmentDescriptors`, `DirectionalLightDescriptors`,
`PerspectiveCameraDescriptors`, `TransformDescriptors` and `GeometryDescriptors`
are the source for these IDs. The table records current DTO defaults, not a request
to reconcile separate design-contract defaults as part of this maintenance refactor.

Final acceptance requires every preserved field mapped to its new owner, every
visual contract mapped to a UI assertion/manual matrix check, all slice exits met,
no unreviewed deviations, no temporary captures/code in the diff, and accurate
documentation/status. Reject a refactor that merely relocates the same monolith,
copies templates, or adds a speculative metadata-driven editor framework.
