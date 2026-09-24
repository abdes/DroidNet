# EX08–EX10 approved scope and execution

Status: scope revision approved and recorded (2026-09-25); implementation not
resumed. The initial four C++ instrumentation drafts were discarded. No code,
new build/test result, capture or UI acceptance is claimed by this revision.

## User decision

Deliver a useful calibrated LightBench and close remaining renderer gaps without
a general experiment/measurement platform. Keep production correctness contracts;
credit applicable existing EX01–EX07 evidence. The agent owns implementation and
unit/native tests. The user owns UI acceptance: launch the demo when ready,
provide numbered actions/expected results, record OK/NOK/reasons and retest only
affected checks. No EX07 capture or benchmark campaign is to be repeated.

## Retained work

- [ ] EX08: calibrated three-card/1000-lux Neutral Reference with independently
      resolved exposure, readable controls and one shared canonical scene
      definition used by the app and focused native reference test.
- [ ] EX08: complete reset, explicit validated local save/load, Reference/Modified
      configuration status and personal UI preference isolation.
- [ ] EX08.1: existing console drives existing settings/transition owners, with
      ordinary Release availability, explicit targets, atomic rejection and
      honest asynchronous status; local preset/reset operations stay in LightBench.
- [ ] EX09A: useful point/spot presets and relevant controls; reuse EX07 proof.
- [ ] EX09B: fixed-exposure interaction on the reference scene plus existing or
      affected native numerical checks.
- [ ] EX09C: simple bright/dark transition/reset; timing and lifecycle matrices
      stay in native tests, with focused gap/regression repair.
- [ ] EX09D: audit applicable HDR evidence and close concrete gaps; no new HDR UI.
- [ ] EX09E: existing MultiView controls/tests/proofs and remaining user checks.
- [ ] EX10: affected final checks, user acceptance, actual commands and durable
      evidence summary; reconcile the tracker and operating docs.

## Removed and deferred work

| Former requirement                                                                     | Disposition                                                                                               |
| -------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| EX08-01–06 reusable GPU region/gain instrument and qualification                       | Removed from package; fixture-level numerical correctness remains required.                               |
| Universal experiment schema/controller and matching UI/batch engine (EX09-01, EX10-01) | Removed; local validated settings and shared canonical scene definition suffice.                          |
| New LightBench runner/report schema (EX10-02)                                          | Removed; use existing tests/output and durable Markdown.                                                  |
| Seven complete experiment UIs and runtime numerical verdicts                           | Removed; retain useful presets/controls, native numerical tests and user inspection.                      |
| Live/on-demand measurement scheduler and instrumented tonemap variant                  | Removed; earlier interactive design approvals do not authorize implementing them under the reduced scope. |
| EX08.2 ImGui Test Engine integration                                                   | Deferred outside package; not complete or a dependency.                                                   |

Earlier build-policy agreement remains a constraint only if future measurement
work is separately approved: explicit build capability independent of NDEBUG,
usable by rebuilding either existing Ninja tree in Release or Debug, with no new
build trees. There is no reason to implement that capability now. Native test
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

Scope audit: all 42 original EX08–EX10 tracker IDs are retained; nine are
explicitly removed, and five EX08.2 entries are deferred. Five new EX08 IDs name
the smaller demo deliverables. Exposure-plan production contract sections 3–6
were compared against HEAD and are textually unchanged apart from whitespace.
Documentation formatting and diff whitespace checks were performed. No build,
test or capture was needed for this documentation-only scope revision.
