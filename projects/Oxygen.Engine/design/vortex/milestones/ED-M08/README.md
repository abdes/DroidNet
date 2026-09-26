# ED-M08 — Native editor rendering extension

Status: `in_progress`

| Field     | Summary                                                                                                                       |
| --------- | ----------------------------------------------------------------------------------------------------------------------------- |
| Outcome   | Native canonical-data implementation has started; rendered/editor gates remain.                                               |
| Remaining | Open: [VX-ED-01](../../OPEN_ITEMS.md#p1--current-delivery).                                                                   |
| Evidence  | [Editor progress](../../../../../../design/editor/IMPLEMENTATION_STATUS.md#ed-m08---runtime-parity-and-standalone-validation) |

Captured-sky/specular IBL is delivered by VX-IBL-01. ED-M08 retains the
remaining native/editor field integration and its broader rendered qualification. The [editor execution plan](../../../../../../design/editor/plan/ED-M08-runtime-parity-and-standalone-validation.md)
owns the eight-slice schedule; its [progress record](../../../../../../design/editor/IMPLEMENTATION_STATUS.md#ed-m08---runtime-parity-and-standalone-validation)
records the current M08.1 work.

The native [VX-IBL-01 plan](../VX-IBL-01/README.md) owns height-fog-aware captured lighting, both automatic update schedules and its DemoShell integration.

The Vortex contracts are [editor rendering](../../lld/editor-rendering.md) and
[captured-sky IBL](../../lld/captured-sky-ibl.md). Track implementation gaps as
[VX-ED-01](../../OPEN_ITEMS.md#p1--current-delivery). The delivered VTX-M08 baseline
is extended by [VX-IBL-01](../VX-IBL-01/README.md) with captured sky, specular
lighting and both automatic update schedules.

## Supporting records

- [Native delivery scope](context.md)
- [Deferred capability contracts](deferred-capabilities.md)
