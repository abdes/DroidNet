# Standalone Runtime Validation LLD

Status: `scaffold`

## 1. Purpose

Define how V0.1 cooked output is launched or loaded by standalone runtime code
to prove that editor-authored scenes are real Oxygen runtime products.

## 2. PRD Traceability

- `GOAL-001`
- `REQ-018`
- `REQ-022`
- `REQ-023`
- `REQ-024`
- `REQ-030`
- `REQ-037`
- `SUCCESS-001`
- `SUCCESS-004`
- `SUCCESS-006`

## 3. Architecture Links

- `ARCHITECTURE.md` sections 2.1, 8.6, 12, 13, 15

## 4. Current Baseline

To be reviewed against current standalone engine examples, cooked scene load
paths, launch tooling, and validation logs.

## 5. Target Design

Standalone validation consumes cooked output and verifies that expected V0.1
authored content loads in a standalone engine context without manual file
repair.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.ContentPipeline` | cooked output and manifest/index validation before launch |
| standalone validation harness | launch/load and expected-content checks |
| `diagnostics-operation-results.md` | visible result and failure-domain mapping |

## 7. Data Contracts

To define:

- standalone validation request
- expected scene content summary
- launch/load result
- parity evidence
- validation failure domain

## 8. Commands, Services, Or Adapters

To define:

- launch standalone runtime
- load cooked scene
- collect runtime summary
- compare expected content
- publish validation result

## 9. UI Surfaces

To define:

- validation command surface
- validation result summary
- technical details/log correlation

## 10. Persistence And Round Trip

Standalone validation does not mutate authoring data. It records evidence for
the current cooked output.

## 11. Live Sync / Cook / Runtime Behavior

Standalone validation runs after cook validation. It is separate from embedded
live preview and must not use live editor runtime state as proof.

## 12. Operation Results And Diagnostics

Failures must identify whether the issue is cooked output, asset resolution,
runtime load, expected-content mismatch, or launch environment.

## 13. Dependency Rules

The validation harness consumes cooked output and runtime launch capabilities.
It must not depend on WorldEditor UI internals.

## 14. Validation Gates

To define:

- cooked scene launches
- expected geometry/material/camera/light/environment summary matches
- failures produce operation result and logs

## 15. Open Issues

- Whether validation is launched from editor UI, command line, or both.
- Exact runtime summary format.
