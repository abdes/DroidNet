# ED-M08 — Native editor rendering extension

Status: `in_progress`

| Field     | Summary                                                                                                                                                                                                                  |
| --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Outcome   | M08.1 canonical data and content pipeline are validated; M08.F1's descriptor-local format cutover is implemented, with editor scene-reference authoring and its qualification open; rendered/editor parity gates remain. |
| Remaining | Open: [VX-ED-01](../../OPEN_ITEMS.md#p1--current-delivery).                                                                                                                                                              |
| Evidence  | [Editor progress](../../../../../../design/editor/IMPLEMENTATION_STATUS.md#ed-m08---runtime-parity-and-standalone-validation)                                                                                            |

Captured-sky/specular IBL is delivered by VX-IBL-01. ED-M08 retains the
remaining native/editor field integration and its broader rendered qualification. The [editor execution plan](../../../../../../design/editor/plan/ED-M08-runtime-parity-and-standalone-validation.md)
owns the slice order, including M08.F1 before M08.2; its [progress record](../../../../../../design/editor/IMPLEMENTATION_STATUS.md#ed-m08---runtime-parity-and-standalone-validation)
records M08.1 as validated and the remaining integration gates as open.

The native [VX-IBL-01 plan](../VX-IBL-01/README.md) owns height-fog-aware captured lighting, both automatic update schedules and its DemoShell integration.

The Vortex contracts are [editor rendering](../../lld/editor-rendering.md) and
[captured-sky IBL](../../lld/captured-sky-ibl.md). Track implementation gaps as
[VX-ED-01](../../OPEN_ITEMS.md#p1--current-delivery). The delivered VTX-M08 baseline
is extended by [VX-IBL-01](../VX-IBL-01/README.md) with captured sky, specular
lighting and both automatic update schedules.

## Supporting records

- [Native delivery scope](context.md)
- [Deferred capability contracts](deferred-capabilities.md)
