# EX08–EX10 approved scope and execution

Status: EX08–EX10 validated and closed (2026-09-25); structured commit delivery authorized after user review.
See [final results and acceptance](EX10-completion.md). The initial four
instrumentation drafts were discarded before the reduced implementation.

## User decision

Deliver a useful calibrated LightBench and close remaining renderer gaps without
a general experiment/measurement platform. Keep production correctness contracts;
credit applicable existing EX01–EX07 evidence. The agent owns implementation and
unit/native tests. The user owns UI acceptance: launch the demo when ready,
provide numbered actions/expected results, record OK/NOK/reasons and retest only
affected checks. No EX07 capture or benchmark campaign is to be repeated.

## Retained work

- [x] EX08: calibrated three-card/1000-lux Neutral Reference with independently
      resolved exposure, readable controls and one shared canonical scene
      definition used by the app and focused native reference test.
- [x] EX08: complete reset, explicit validated local save/load, Reference/Modified
      configuration status and personal UI preference isolation.
- [x] EX08.1: existing console drives existing settings/transition owners, with
      ordinary Release availability, explicit targets, atomic rejection and
      honest asynchronous status; local preset/reset operations stay in LightBench.
- [x] EX09A: useful point/spot presets and relevant controls; reuse EX07 proof.
- [x] EX09B: fixed-exposure interaction on the reference scene plus existing or
      affected native numerical checks.
- [x] EX09C: simple bright/dark transition/reset; timing and lifecycle matrices
      stay in native tests, with focused gap/regression repair.
- [x] EX09D: audit applicable HDR evidence and close concrete gaps; no new HDR UI.
- [x] EX09E: existing MultiView controls/tests/proofs and user checks.
- [x] EX08.2: build-configured real-app Test Engine workflows, appropriate test migration,
      Debug/Release qualification and ordinary-build isolation.
- [x] EX10: affected final checks, user acceptance, actual commands and durable
      evidence summary; reconcile the tracker and operating docs.

## Removed and deferred work

| Former requirement                                                                     | Disposition                                                                                                    |
| -------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------- |
| EX08-01–06 reusable GPU region/gain instrument and qualification                       | Removed from package; fixture-level numerical correctness remains required.                                    |
| Universal experiment schema/controller and matching UI/batch engine (EX09-01, EX10-01) | Removed; local validated settings and shared canonical scene definition suffice.                               |
| General measurement/batch runner and report schema (EX10-02)                           | Removed; use existing numerical tests/output and durable Markdown. EX08.2's widget runner uses standard JUnit. |
| Seven complete experiment UIs and runtime numerical verdicts                           | Removed; retain useful presets/controls, native numerical tests and user inspection.                           |
| Live/on-demand measurement scheduler and instrumented tonemap variant                  | Removed; earlier interactive design approvals do not authorize implementing them under the reduced scope.      |

The earlier EX08.2 deferral was superseded by the user's goal. Its explicit
build option works independently of NDEBUG in the existing Ninja trees and is
off in ordinary builds. Both trees have been restored without UI instrumentation.
This does not restore the removed measurement platform. Native numerical
readbacks remain test-owned; ordinary rendering gains no new diagnostic work.

## Acceptance limits and retained guarantees

The app does not numerically certify arbitrary modified frames. Native tests
qualify the canonical scene against independent production-BRDF/encoding budgets;
the user accepts visual behavior and interaction. The reference test must share
scene construction/parameters with the app, not maintain an approximate duplicate.

Reset restores scene/camera/lights/materials/environment/rendering/exposure and
relevant temporal state. It preserves window/panel preferences. Saved modifications
load explicitly, validate atomically and remain labelled Modified. No runtime GPU
identities/history are serialized. Ordinary console commands remain governed by
existing access policies; commands never write GPU state directly.

Numerical tolerances, HDR/lifecycle/sharing semantics and the obligation to fix
discovered product bugs are unchanged. Reuse requires applicable inputs and
relevant implementation identities. Removed requirements are never marked as
implemented; remaining work closes only with code, evidence and documentation.

## Document reconciliation

The delivery plan, item tracker, top-level plan, LightBench specification and
Diagnostics LLD reflect this decision. Sections 3–6 of the exposure plan retain
the production mathematical/ownership contracts. EX01–EX07 closure and immutable
historical evidence remain unchanged. No reviewer-owned documents are included.

Scope audit: all 42 original EX08–EX10 tracker IDs are retained, with five new
EX08 IDs for the smaller demo. Nine removed requirements remain removed; the
other 38 entries are validated, including the five reactivated EX08.2 entries.
The original scope revision preserved production contract sections 3–6; final
build/test/acceptance evidence is recorded separately in EX10.

## User priority update: usable EX09 presets

The user prioritized EX09 scenarios after the saved-scene Auto diagnosis.
EX08.1 and the subsequently reactivated EX08.2 were also completed before EX10.
Neutral Reference remains the manual numerical reference; Point Falloff, Spot
Cone, Material Lighting and Auto Adaptation stage complete settings rather than
inherit accidental state from a previous scenario. Reset restores the selected
preset. Auto light steps change illumination without recreating exposure history.
No generic controller, new runtime measurements or automatic UI testing is added.
EX09D/E and the group closure still need their retained evidence/operational gates.

Indoor and Outdoor Daylight were added at the user's explicit request. The
[EX09 preset result](EX09-presets-results.md) records implementation, native
qualification, the corrected step-test expectation and outstanding visual gate.

## Active completion order (user goal, 2026-09-25)

Finish EX09, then return to EX08.2, then perform EX10 closeout. This instruction
reactivates ImGui Test Engine integration and its named widget workflows after
EX09; earlier deferral text above records the preceding scope decision. EX08.1
console controls remain a retained, unfinished obligation and may not be silently
waived at closeout. No runtime measurement/controller infrastructure is restored.
Use only the existing Ninja trees; test-only UI hooks must be explicitly isolated
so normal Release builds do not acquire test instrumentation overhead.

EX09's native HDR/lifecycle coverage, local settings/UI checks and MultiView
operational acceptance are complete. Spot full-profile verification and all
seven preset images remain applicable; they were not recaptured for closeout.

EX08.1 and EX08.2 are complete before EX10. The small permanent preset overlay
has margin, rounded corners, a scrollbar-free seven-item popup and an amber
modified-state border. Full-window scene rendering and translucent sidebar
overlays remain the established contract. Structured commits were authorized
after qualification and user review.
