# VTX-FUTURE — Reserved post-baseline families

Status: `future`

| Field     | Summary                                                                                                                                                                                                |
| --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Outcome   | Unscheduled renderer families; no aggregate implementation milestone.                                                                                                                                  |
| Remaining | Open: [VX-FAMILY-01](../../OPEN_ITEMS.md#p3--unscheduled-capabilities), [VX-VIEW-02](../../OPEN_ITEMS.md#p3--unscheduled-capabilities), [VX-SKY-01](../../OPEN_ITEMS.md#p3--unscheduled-capabilities). |
| Evidence  | [Validation record](validation.md)                                                                                                                                                                     |

[Roadmap](../../PLAN.md) · [Design index](../../lld/README.md)

Dependencies: VTX-M08 or explicit reprioritization

## Delivered scope

Geometry virtualization, material composition, broader indirect lighting/GI/reflections, VSM, clouds, heterogeneous volumes, water, hair, distortion, light shafts.

## Deferred capability owners

- Captured-sky diffuse/specular IBL and specified-cubemap specular lighting are
  scheduled under [ED-M08](../ED-M08/README.md), including Stage 13 activation and
  retirement of the Stage 12 ambient bridge. Implementation and rendered proof
  were pending in the preserved baseline.
- Broader GI/SSR, reflection probes, continuous time-sliced capture, SkyLight
  occlusion/baking and cubemap blending require separate delivery plans.
- Reflection/360-view aerial perspective still needs a runtime resource path.
- Further exposure CPU optimization is defined in [EX05.1](../exposure/EX05.1/README.md).

## Validation

See the [validation record](validation.md) for commands, conditions, results and remaining work.
