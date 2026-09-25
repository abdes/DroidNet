# EX08.1 — Post-processing console controls

Status: `validated`

| Field     | Summary                                                                       |
| --------- | ----------------------------------------------------------------------------- |
| Outcome   | Console settings and transitions through the same validated owners as the UI. |
| Remaining | None in the recorded scope.                                                   |
| Evidence  | [Validation record](validation.md)                                            |

[Roadmap](../../../PLAN.md) · [Design index](../../../lld/README.md)

## Delivery scope

**Dependency:** EX08. **Tracked by:** EX081-01–04/GATE.

Use the existing console namespace/help/completion, access policies and
`CommandSource::kAutomation`. Commands inspect/edit exposure, camera inputs,
mask/curve and output settings, and request seed/remeter transitions through the
same validated services as UI. Keep explicit view/owner targeting and truthful
queued/applied/rejected revision/token/error feedback. Invalid complete requests
must leave accepted state unchanged. A queued mask is not immediate success.

LightBench preset/reset commands call its local operations directly; no general
experiment adapter is required. Ordinary post-process commands remain available
in normal Release builds under existing policies. There are no new measurement
commands. Test valid/invalid inputs, stale targets, lifecycle unregistration and
transition ordering. The user tests the integrated console UI from a checklist.

**Exit gate:** documented commands and UI converge on the same accepted settings
and transitions; native tests cover validation/lifetime; user confirms visible
behavior. No measured-output service or new persistence path is needed.

## Recorded qualification

| Slice | Status | Boundary | Evidence |
| ----- | ------ | -------- | -------- |

| 8.1 — Console controls | validated | Shared validated owners, explicit targets, asynchronous status, native/widget checks and acceptance. | [Console results](validation.md) |

## Tasks and outcome

**Validated, including real-console widget tests and user exposure/console checks.**
Ordinary controls remain available in normal Release under existing console
policies; no new measurement commands or framework. See the
[66/66 Debug and Release results and review guide](validation.md).

| ID         | Required result                                                                                                             | Status    |
| ---------- | --------------------------------------------------------------------------------------------------------------------------- | --------- |
| EX081-01   | Existing namespace/help/completion/access policy and explicit view/owner targeting.                                         | validated |
| EX081-02   | Settings, camera/mask/curve/output edits and seed/remeter use existing validated owners.                                    | validated |
| EX081-03   | Local LightBench preset/reset calls and truthful async revision/token/error reporting.                                      | validated |
| EX081-04   | Valid/invalid/stale-target tests, automation execution, unregistration and operating commands.                              | validated |
| EX081-GATE | UI/console converge on accepted settings/transitions with atomic rejection; integrated console UI passes manual acceptance. | validated |

## Supporting records

- [validation](validation.md)
- [evidence](evidence/README.md)
- [Captured evidence](evidence/README.md)
