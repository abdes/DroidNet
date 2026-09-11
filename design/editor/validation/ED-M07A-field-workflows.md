# ED-M07A Field And Workflow Results

## Environment controls

The packaged inspector suite passes **82/82**. The 40 cases in
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

Runtime tests pass **66/66**, including the real native environment round trip,
scene/run invalidation, cancellation, failure reporting and retry.

## Remaining M07A.6 cases

- Transform, camera and directional-light field workflows.
- Geometry and material-slot pick, missing URI, clear and mixed-value workflows.
- Component transitions, defaults/null environment, and runtime FPS/logging rejection.
- Final viewport qualification.
