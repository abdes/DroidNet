# EX07A — Contracts

Status: `validated`

| Field     | Summary                                                            |
| --------- | ------------------------------------------------------------------ |
| Outcome   | Lighting/property/ABI decisions and canonical interface migration. |
| Remaining | None in the recorded scope.                                        |
| Evidence  | [Validation record](validation.md)                                 |

[Roadmap](../../../../PLAN.md) · [Design index](../../../../lld/README.md)

## Design

See [lighting decisions](../../../../lld/lighting-decisions.md), the [GPU ABI](../../../../lld/lighting-gpu-abi.md) and [validation](validation.md).

### Contract and property review deliverables

EX07A must complete the [canonical LightingService contract](../../../../lld/lighting-service.md#2-canonical-data-and-interfaces)
and its [authored-property inventory](../../../../lld/lighting-service.md#5-authored-property-coverage).
For each retained field, identify its source/editor/script/native ingress,
validation/default, persisted representation, selection/GPU member, consumer,
invalidation and positive/negative/round-trip tests. Include per-light compensation,
source radius, cone pairs and all supported shadow settings; defaults alone cannot
qualify these fields. Approved [EX07A D2](../../../../lld/lighting-decisions.md#d2--physical-lighting-and-artistic-attenuation-scope)
removes the attenuation selector and custom decay exponent through a strict
API/source/packed/tooling migration; it supersedes their earlier retention
requirement. Track rejection of obsolete inputs and migration of every producer.
D3 uses analytic source-size diffuse/specular response with center-based range and
cone support. D4 uses shared correlated Smith GGX, Schlick Fresnel and
view-dependent multiple-scattering compensation. Direct and indirect consumers
share the compact energy texture and material interpretation. Compare the
approximations with independent references and report image, energy and lobe
differences alongside timing and memory.

Freeze CPU/HLSL field offsets/stride/types and add sentinel decode tests before
connecting the culling shader. Describe shared preparation versus per-view GPU
recording, upload/compute/read dependencies and frame-fence lifetime. The
[capacity/failure contract](../../../../lld/lighting-service.md#4-capacity-failure-and-recovery)
defines candidate rejection, invalid view results, caller diagnostics and recovery;
fault tests must verify the actual application outcome. No unresolved capacity
choice can be silently made while optimizing.

## Supporting records

- [validation](validation.md)
