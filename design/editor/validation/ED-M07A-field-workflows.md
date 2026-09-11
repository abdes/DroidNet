# ED-M07A Field And Workflow Results

## Environment controls

The packaged inspector suite passes **137/137**. The 40 cases in
`EnvironmentFieldControlHistoryAndReopenReachNativeState` edit the actual numeric,
vector, toggle, checkbox and enum controls. Each edit creates one history entry.
Every case compares all source and native fields after editing, Undo, Redo and
atomic Save/reopen; untouched fields retain their values. Current field
diagnostics remain clear.

The input column uses inspector units. Source/native lengths are metres,
EV fields use stops, and RGB values are linear unless stated otherwise.

| Field | Control input | Source/native value | Undo/Redo | Save/reopen | Feedback |
| --- | --- | --- | --- | --- | --- |
| `AtmosphereEnabled` | false | false | Pass | Pass | Clear |
| `SunDiskEnabled` | false | false | Pass | Pass | Clear |
| `PlanetRadiusKm` | 6400 | 6400000 | Pass | Pass | Clear |
| `AtmosphereHeightKm` | 90 | 90000 | Pass | Pass | Clear |
| `GroundAlbedoR` | 0.15 | 0.15 | Pass | Pass | Clear |
| `GroundAlbedoG` | 0.25 | 0.25 | Pass | Pass | Clear |
| `GroundAlbedoB` | 0.35 | 0.35 | Pass | Pass | Clear |
| `RayleighScaleHeightKm` | 7 | 7000 | Pass | Pass | Clear |
| `MieScaleHeightKm` | 1.4 | 1400 | Pass | Pass | Clear |
| `MieAnisotropy` | 0.7 | 0.7 | Pass | Pass | Clear |
| `SkyLuminanceR` | 0.15 | 0.15 | Pass | Pass | Clear |
| `SkyLuminanceG` | 0.25 | 0.25 | Pass | Pass | Clear |
| `SkyLuminanceB` | 0.35 | 0.35 | Pass | Pass | Clear |
| `AerialPerspectiveDistanceScale` | 1.2 | 1.2 | Pass | Pass | Clear |
| `AerialScatteringStrength` | 0.8 | 0.8 | Pass | Pass | Clear |
| `AerialPerspectiveStartDepthMeters` | 40 | 40 | Pass | Pass | Clear |
| `HeightFogContribution` | 0.6 | 0.6 | Pass | Pass | Clear |
| `ExposureMode` | ManualCamera (1) | ManualCamera (1) | Pass | Pass | Clear |
| `ExposureEnabled` | false | false | Pass | Pass | Clear |
| `ExposureKey` | 11 | 11 | Pass | Pass | Clear |
| `ManualExposureEv` | 5.5 | 5.5 | Pass | Pass | Clear |
| `ExposureCompensation` | 1.25 | 1.25 | Pass | Pass | Clear |
| `ToneMapping` | Reinhard (3) | Reinhard (3) | Pass | Pass | Clear |
| `AutoExposureMeteringMode` | Spot (2) | Spot (2) | Pass | Pass | Clear |
| `AutoExposureMinEv` | -3 | -3 | Pass | Pass | Clear |
| `AutoExposureMaxEv` | 14 | 14 | Pass | Pass | Clear |
| `AutoExposureSpeedUp` | 4 | 4 | Pass | Pass | Clear |
| `AutoExposureSpeedDown` | 2 | 2 | Pass | Pass | Clear |
| `AutoExposureLowPercentile` | 0.2 | 0.2 | Pass | Pass | Clear |
| `AutoExposureHighPercentile` | 0.8 | 0.8 | Pass | Pass | Clear |
| `AutoExposureMinLogLuminance` | -10 | -10 | Pass | Pass | Clear |
| `AutoExposureLogLuminanceRange` | 20 | 20 | Pass | Pass | Clear |
| `AutoExposureTargetLuminance` | 0.25 | 0.25 | Pass | Pass | Clear |
| `AutoExposureSpotMeterRadius` | 0.4 | 0.4 | Pass | Pass | Clear |
| `BloomIntensity` | 0.7 | 0.7 | Pass | Pass | Clear |
| `BloomThreshold` | 1.5 | 1.5 | Pass | Pass | Clear |
| `Saturation` | 0.9 | 0.9 | Pass | Pass | Clear |
| `Contrast` | 1.1 | 1.1 | Pass | Pass | Clear |
| `VignetteIntensity` | 0.3 | 0.3 | Pass | Pass | Clear |
| `DisplayGamma` | 2.4 | 2.4 | Pass | Pass | Clear |

`BackgroundPickerUndoRedoAndReopenReachTheObservedNativeScene` separately passes
picker sRGB `(100, 64, 128)` through linear source/native RGB
`(0.12743768, 0.05126946, 0.2158605)`, Undo/Redo and Save/reopen. The user verified
picker matching in the viewport, independence from exposure and tone mapping,
and restoration through history and saved source.

Runtime tests pass **71/71**, including the real native environment round trip,
scene/run invalidation, cancellation, failure reporting and retry.

## Transform, camera and directional-light controls

All 38 `NodeFieldControlHistoryAndReopenReachNativeState` cases pass. Each uses
the actual inspector control, creates one history entry, compares every source
and native component field, and repeats the comparison after Undo, Redo and
atomic Save/reopen. Field diagnostics remain clear. Each axis listed below is
tested independently.

| Component | Field | Control input | Source/native value | Undo/Redo | Save/reopen |
| --- | --- | --- | --- | --- | --- |
| Transform | Position X/Y/Z | 12 | 12 m | Pass | Pass |
| Transform | Rotation X/Y/Z | 30 | 30 degrees, Y-X-Z convention | Pass | Pass |
| Transform | Scale X/Y/Z | 2 | 2 | Pass | Pass |
| Camera | FieldOfView | 75 degrees | 75 degrees / 1.308997 radians | Pass | Pass |
| Camera | AspectRatio | 1.5 | 1.5 | Pass | Pass |
| Camera | NearPlane | 0.5 | 0.5 m | Pass | Pass |
| Camera | FarPlane | 2000 | 2000 m | Pass | Pass |
| Light | Color R/G/B | 0.2 / 0.3 / 0.4 | Linear RGB, same values | Pass | Pass |
| Light | AffectsWorld | false | false | Pass | Pass |
| Light | Mobility | Mixed | Mixed (1) | Pass | Pass |
| Light | CastsShadows | false | false | Pass | Pass |
| Light | ShadowBias | 0.01 | 0.01 | Pass | Pass |
| Light | ShadowNormalBias | 0.04 | 0.04 | Pass | Pass |
| Light | ContactShadows | true | true | Pass | Pass |
| Light | ShadowResolutionHint | High | High (2) | Pass | Pass |
| Light | ExposureCompensation | 1.5 | 1.5 EV | Pass | Pass |
| Light | IntensityLux | 80000 | 80000 lux | Pass | Pass |
| Light | AngularSizeRadians | 0.02 | 0.02 radians | Pass | Pass |
| Light | EnvironmentContribution | false | false; Sun also false | Pass | Pass |
| Light | IsSunLight | false | false | Pass | Pass |
| Light | CascadeCount | 3 | 3 | Pass | Pass |
| Light | SplitMode | ManualDistances | ManualDistances (1) | Pass | Pass |
| Light | MaxShadowDistance | 200 | 200 m | Pass | Pass |
| Light | CascadeDistance 1/2/3/4 | 12 / 32 / 90 / 220 | Same distances in metres | Pass | Pass |
| Light | DistributionExponent | 2 | 2 | Pass | Pass |
| Light | TransitionFraction | 0.2 | 0.2 | Pass | Pass |
| Light | DistanceFadeoutFraction | 0.3 | 0.3 | Pass | Pass |

Three composed-rotation control cases `(20,30,40)`, `(100,10,20)` and
`(-100,30,-20)` retain their native orientation after Save/reopen. Separate
one-shot and gesture history tests restore the full quaternion at gimbal lock.

The Sun and Contributes switches and the scene sun picker pass three two-light
cases. Enabling Sun enables contribution and replaces the old sun; disabling
Contributes clears Sun and its scene binding. Native flags, primary-sun selection,
both original lights and the scene binding survive Undo/Redo and Save/reopen.

## Asset and transition workflows

| Case | Native/source result | History and saved source | Feedback |
| --- | --- | --- | --- |
| Geometry mixed selection: Cube + Plane to Sphere | Both native meshes resolve to Sphere with vertices, indices and asset keys | Undo restores Cube + Plane; Redo and Save/reopen retain Sphere | Mixed `--` and Sphere labels follow source |
| Material mixed selection: cooked Original + no override to cooked New | Both slots resolve the imported New asset key | Undo restores each original; Redo and Save/reopen retain New | Picker changes between `--` and New |
| Material None | Native slots resolve their default material after clearing | Undo restores mixed originals; Redo and Save/reopen preserve the clear | Picker changes between `--` and None |
| Material Default | Generated Default resolves directly to the engine material | Undo restores mixed originals; Redo and Save/reopen retain Default | Picker changes between `--` and Default |
| Missing geometry URI | Authored URI remains; failed replacement retains the last resolved mesh; reopened scene has no resolved mesh | Undo restores Cube; Redo and Save/reopen reproduce the missing reference | Native failure includes document, lifetime, node, URI and geometry diagnostic code; picker shows `DoesNotExist.ogeo` |
| Camera and directional-light add/remove | Native component fields appear and disappear with source | Add and remove pass Undo/Redo and Save/reopen | Inspector sections follow the component set |
| Locked Transform removal | Transform remains in source and native state | Rejected command creates no history | Delete control is disabled |
| Empty selection with default or missing environment data | Scene settings open; all normalized defaults match native state | Save/reopen preserves defaults with no history | Environment inspector opens with clear field diagnostics |
| Stale sun node/component removal and restoration | Authored sun reference remains until explicit clear | Restoration recovers the selected target | Actual picker shows and clears the unresolved warning |
| Runtime FPS | Slider sets native target to 45; after shutdown, setting 30 is rejected | Runtime setting creates no scene history or dirty state | Document-scoped `TARGET_FPS_REJECTED` result |
| Runtime logging | NumberBox sets native verbosity to 2; after shutdown, setting -3 is rejected | Runtime setting creates no scene history or dirty state | Document-scoped `LOGGING_VERBOSITY_REJECTED` result |

Material fixtures use the production importer and mounted cooked index. The
managed material writer now emits the engine's 357-byte descriptor, including
its 103-byte asset header and extension defaults. Managed.Assets tests pass 89/89;
World serialization tests pass 67/67; SceneExplorer tests pass 161/161.

## Viewport qualification

On 2026-09-11, the user confirmed all three checks in the restarted Debug editor
through Undo/Redo and Save/reopen:

- Combined XYZ rotation.
- Cube/Sphere changes with a newly cooked material, None and Default.
- Sun/Contributes automatically staying consistent.

Background presentation was verified earlier. All M07A.6 gates pass.
