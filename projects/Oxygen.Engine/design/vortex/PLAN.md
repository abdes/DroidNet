# Vortex milestone roadmap

The milestone links below own scope, status, acceptance and validation. This
roadmap records sequence and dependencies; update the milestone when work changes.

See [STATUS.md](STATUS.md) for progress and [OPEN_ITEMS.md](OPEN_ITEMS.md) for pending, incomplete and deferred work.

## Delivery packages

- [Desktop renderer baseline](#desktop-renderer-baseline): VTX-M00 through VTX-M08.
- [Exposure and LightBench](milestones/exposure/README.md): EX01–EX10, including
  the inserted performance, quality, console and widget-automation slices.
- [ED-M08 native editor extension](milestones/ED-M08/README.md): native authoring,
  captured-sky IBL and editor integration; the editor plan owns its schedule.
- [Material sidedness correction](milestones/material-sidedness/README.md).
- [RenderScene preview sun](milestones/preview-sun/README.md).
- [Future capabilities](milestones/VTX-FUTURE/README.md).

## Desktop renderer baseline

| Milestone                                     | Scope                                                | Dependencies                                                                              |
| --------------------------------------------- | ---------------------------------------------------- | ----------------------------------------------------------------------------------------- |
| [VTX-M00](milestones/VTX-M00/README.md)       | Planning and status truth surface                    | current docs/source                                                                       |
| [VTX-M01](milestones/VTX-M01/README.md)       | Renderer Core and SceneRenderer baseline             | none                                                                                      |
| [VTX-M02](milestones/VTX-M02/README.md)       | Deferred core visual path                            | VTX-M01                                                                                   |
| [VTX-M03](milestones/VTX-M03/README.md)       | Migration-critical non-environment services          | VTX-M02                                                                                   |
| [VTX-M04D](milestones/VTX-M04D/README.md)     | Environment / fog parity closure                     | VTX-M02, VTX-M03 validation context                                                       |
| [VTX-M04D.1](milestones/VTX-M04D.1/README.md) | Environment publication and sky/fog contract truth   | VTX-M02, current environment code                                                         |
| [VTX-M04D.2](milestones/VTX-M04D.2/README.md) | UE5.7 exponential height fog parity                  | VTX-M04D.1                                                                                |
| [VTX-M04D.3](milestones/VTX-M04D.3/README.md) | UE5.7 local fog volume parity                        | VTX-M04D.1, VTX-M04D.2, Stage 5 HZB                                                       |
| [VTX-M04D.4](milestones/VTX-M04D.4/README.md) | UE5.7 volumetric fog parity                          | VTX-M04D.1, VTX-M04D.2, VTX-M04D.3, VTX-M03 directional CSM proof                         |
| [VTX-M04D.5](milestones/VTX-M04D.5/README.md) | Environment runtime proof and Async preparation      | VTX-M04D.2, VTX-M04D.3, VTX-M04D.4, VTX-M04D.6                                            |
| [VTX-M04D.6](milestones/VTX-M04D.6/README.md) | UE5.7 aerial perspective parity                      | VTX-M04D.1, VTX-M04D.2                                                                    |
| [VTX-M04E](milestones/VTX-M04E/README.md)     | Async migration parity gate                          | VTX-M03, VTX-M04D.5                                                                       |
| [VTX-M04F](milestones/VTX-M04F/README.md)     | Single-view composition and presentation closeout    | VTX-M04E                                                                                  |
| [VTX-M05A](milestones/VTX-M05A/README.md)     | Diagnostics product service                          | VTX-M04D.1, VTX-M04F                                                                      |
| [VTX-M05B](milestones/VTX-M05B/README.md)     | Occlusion consumer closeout                          | VTX-M02                                                                                   |
| [VTX-M05C](milestones/VTX-M05C/README.md)     | Translucency stage                                   | VTX-M03, VTX-M04D.5, VTX-M05A, VTX-M05B                                                   |
| [VTX-M05D](milestones/VTX-M05D/README.md)     | Conventional shadow parity and local-light expansion | VTX-M03, VTX-M05A, VTX-M05B, VTX-M05C                                                     |
| [VTX-M06A](milestones/VTX-M06A/README.md)     | Multi-view proof closeout                            | VTX-M05A, VTX-M05B, VTX-M05C, VTX-M05D                                                    |
| [VTX-M06B](milestones/VTX-M06B/README.md)     | Offscreen proof closeout                             | VTX-M05A, VTX-M05C                                                                        |
| [VTX-M06C](milestones/VTX-M06C/README.md)     | Feature-gated runtime variants                       | VTX-M06A, VTX-M06B                                                                        |
| [VTX-M07](milestones/VTX-M07/README.md)       | Production readiness and legacy retirement           | VTX-M06C                                                                                  |
| [VTX-M08](milestones/VTX-M08/README.md)       | Skybox and static specified-cubemap SkyLight         | VTX-M07, VTX-M04D environment publication truth, VTX-M05D shadows, VTX-M06C feature gates |
| [VTX-FUTURE](milestones/VTX-FUTURE/README.md) | Reserved post-baseline families                      | VTX-M08 or explicit reprioritization                                                      |

## Dependencies

```text
VTX-M00
  └─► VTX-M01
       └─► VTX-M02
            ├─► VTX-M03
            └─► VTX-M04D.1
                 ├─► VTX-M04D.2
                 │    ├─► VTX-M04D.3
                 │    │    └─► VTX-M04D.4
                 │    └─► VTX-M04D.6
                 └─► VTX-M05A

VTX-M04D.2 + VTX-M04D.3 + VTX-M04D.4 + VTX-M04D.6
  └─► VTX-M04D.5

VTX-M03 + VTX-M04D.5
  └─► VTX-M04E
       └─► VTX-M04F
            ├─► VTX-M05A
            ├─► VTX-M05B
            ├─► VTX-M05C
            └─► VTX-M05D
                 └─► VTX-M06A / VTX-M06B
                      └─► VTX-M06C
                           └─► VTX-M07
                                └─► VTX-M08
```

Parallelism rules:

- VTX-M04D.2 height fog and VTX-M04D.3 local fog can overlap after contract
  truth is complete, but their final parity claims must account for coupling.
- VTX-M04D.6 aerial perspective can start after height-fog cleanup because it
  must settle whether AP contains fog contribution or only composes with the
  separate height-fog pass. Any resulting publication or height-fog contract
  change must be propagated back to VTX-M04D.1 and VTX-M04D.2 before AP parity
  can be claimed.
- VTX-M05A diagnostics can start once the environment and renderer publication
  surfaces it inspects are stable enough; it must not reach into private
  service internals.
- VTX-M05B occlusion can proceed independently of environment after VTX-M02.
- VTX-M05C translucency should wait for lighting/shadow/environment
  publications to be stable.
- Multi-view and offscreen remain separate proof milestones.

See [capability boundaries](milestones/capabilities.md) for feature activation and deferred families.
