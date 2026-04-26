# Diagnostics And Operation Results LLD

Status: `scaffold`

## 1. Purpose

Define operation results, diagnostic records, failure-domain classification,
presentation rules, and log correlation for user-triggered editor workflows.

## 2. PRD Traceability

- `REQ-022`
- `REQ-023`
- `REQ-024`
- `SUCCESS-006`
- `SUCCESS-009`

## 3. Architecture Links

- `ARCHITECTURE.md` sections 3.6, 8, 13, 15, 17

## 4. Current Baseline

To be reviewed against current logging, output panel, exception handling,
pipeline diagnostics, scene validation, and runtime failure surfaces.

## 5. Target Design

Logs remain required, but user-triggered workflow failures must produce visible
operation results with enough context for the user to continue.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| owning subsystem | creates operation result and diagnostics for its workflow |
| diagnostics services | common result model, classification, aggregation |
| UI feature | presents result near the workflow surface |
| output/log panel | shows technical details and log correlation |

## 7. Data Contracts

To define:

- operation result
- diagnostic record
- failure domain
- severity
- affected project/document/asset/node/component identity
- log correlation token
- suggested next action

## 8. Commands, Services, Or Adapters

To define:

- result publisher
- diagnostic store
- subsystem result adapters
- log correlation adapter
- optional fix-action contract

## 9. UI Surfaces

To define:

- inline operation result
- output panel entry
- status bar summary
- blocking modal policy, if any
- diagnostics list/detail view

## 10. Persistence And Round Trip

Diagnostics are runtime/editor state unless explicitly saved as project reports.
The LLD must define which diagnostics survive restart, if any.

## 11. Live Sync / Cook / Runtime Behavior

Live sync, cook, mount, and runtime failures must classify their failure domain
instead of collapsing into generic exceptions.

## 12. Operation Results And Diagnostics

This LLD owns the shared vocabulary and model for operation results. Individual
subsystem LLDs own when they emit results.

## 13. Dependency Rules

Diagnostics model must not depend on feature UI internals. Feature UI may
present diagnostics through common contracts.

## 14. Validation Gates

To define:

- save failure visible
- cook failure visible
- mount failure visible
- sync failure visible
- runtime surface/view failure visible
- logs correlate with user result

## 15. Open Issues

- Whether diagnostics use a central store, per-document store, or hybrid.
- Fix action model for V0.1.
