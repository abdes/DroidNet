# DemoShell console commands

Open the console with **`** (grave accent), or use **Ctrl+Shift+P** for the
command palette. Built-in `help`, `find`, completion and source policies apply.
These controls are available in ordinary Release builds; no instrumentation
option is required.

## Panel commands

Every DemoShell panel control runs a command; the console, the command palette
and a remote client can run the same command. A command posted from a panel
applies at the next frame start. `find <prefix>` lists a group and
`help <command>` shows its parameters.

| Prefix           | Panel or example                                                            |
| ---------------- | --------------------------------------------------------------------------- |
| `demo.camera.*`  | Camera: mode (`orbit`, `fly`, `drone`), projection, lens, drone path, axes. |
| `demo.content.*` | Content Loader: import options, library, scene loading, pak mounting.       |
| `demo.diag.*`    | Diagnostics: frame diagnostics, GPU draw bounds, wireframe color.           |
| `demo.env.*`     | Environment: profile, sun, sky atmosphere, sky sphere, sky light, fog.      |
| `demo.grid.*`    | Ground grid.                                                                |
| `pp.*`           | Post Process: the post-process commands below.                              |
| `async.*`        | Async: tour playback, lighting preset, animation, spotlight.                |
| `cube.*`         | TexturedCube: material sandbox, texture import, skybox.                     |
| `input.swimming` | InputSystem: swimming mode.                                                 |
| `lightbench.*`   | LightBench: presets, settings file, test object and lights.                 |
| `multiview.fog`  | MultiView: fog.                                                             |
| `physics.*`      | Physics: launch, reset, launch impulse.                                     |

Content's cache trim runs the engine's `cntt.trim_cache`. The view mode, show
flags, stats and GPU work overlay belong to the host's toolbar and its
`host.*` console variables. They are saved in the example's console
variable archive, and a value given on the command line wins over the saved
one.

## Post-process commands

Start with `pp.targets`. It reports:

- `scene:<revision>`: the currently bound DemoShell scene authoring owner.
  Its settings are inherited by views that do not provide their own overrides.
- Published exposure **owner handles**: persistent renderer owners eligible for
  seed/remeter requests. Borrowers and stateless views are excluded.

Use the exact current values. Preset loading or scene replacement changes the
scene revision and can retire exposure handles. Old targets are rejected rather
than silently retargeted. Proof recipes may own per-view overrides; `pp.inspect`
reports that fact. Editing the scene does not overwrite a producer's override.

### Commands

Every command takes typed parameters, so `cmd.list` describes them to remote
clients. On the console the target comes first and the rest by position or as
`name=value`; a remote client sends `{"target": "scene:3", "mode": "auto"}`.

| Command                                                         | Effect                                                                                                                               |
| --------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| `pp.targets`                                                    | Discover the current scene token and exposure owner handles.                                                                         |
| `pp.inspect scene:<revision>`                                   | Read authored exposure, camera/output values and actual renderer acceptance/mask status.                                             |
| `pp.exposure scene:<revision> field=value [...]`                | Validate and commit a complete exposure patch atomically. Duplicate/unknown fields or invalid combinations leave settings unchanged. |
| `pp.camera scene:<revision> aperture_f shutter_rate iso`        | Validate the complete physical-camera exposure triple before changing any field.                                                     |
| `pp.output scene:<revision> none\|aces\|filmic\|reinhard gamma` | Apply a validated tone curve/gamma pair. `none` disables the tone curve, not exposure.                                               |
| `pp.curve scene:<revision> clear`                               | Remove the compensation curve.                                                                                                       |
| `pp.curve scene:<revision> ev:compensation [...]`               | Replace the entire curve; at most 64 ordered keys, validated by the existing owner.                                                  |
| `pp.reset_auto scene:<revision>`                                | Reset the automatic exposure controls; mode, compensation, calibration and camera settings are kept.                                 |
| `pp.transition <owner-handle> remeter`                          | Queue an event-frame remeter.                                                                                                        |
| `pp.transition <owner-handle> seed <ev100>`                     | Queue an explicit exposure seed.                                                                                                     |
| `pp.transition <owner-handle> preserve`                         | Queue the existing preserve policy.                                                                                                  |
| `pp.transition.status <owner-handle>`                           | Inspect the latest renderer-issued request, lifetime, generation, phase, applied generation and error.                               |

Exposure fields use the existing canonical settings:

`manual_ev`, `compensation_ev`, `key`, `min_ev`, `max_ev`, `speed_up`,
`speed_down`, `low_percentile`, `high_percentile`, `min_log_luminance`,
`log_luminance_range`, `target_luminance`, `spot_meter_radius`,
`black_influence`, `transition_distance`.

Additional fields are `enabled=true|false`, `mode=manual|camera|auto`,
`metering=average|center|spot`, and `mask=scene|off`. A scene mask must already
be authored; these commands do not reinterpret a foreign runtime resource key.
EV/compensation units are stops, speeds are EV/s, aperture is f-number, shutter
is reciprocal seconds and camera sensitivity is ISO.

For example, after substituting the target from `pp.targets`:

```text
pp.exposure scene:1 mode=auto min_ev=-6 max_ev=16 speed_up=3 speed_down=1
pp.output scene:1 aces 2.2
pp.curve scene:1 -4:-1 0:0 12:1
pp.inspect scene:1
pp.transition 1 seed 8
pp.transition.status 1
```

An authored edit being accepted is **not** a claim that a frame has consumed it.
Settings are captured through the existing renderer path. Mask residency may
remain Pending/Failed while older accepted settings remain active. The authored
edit epoch and renderer acceptance revision are separate identities.
`pp.inspect` reports whether accepted exposure settings match the authored ones.
It does not report a measured GPU Auto gain or certify arbitrary images.

A transition reports **queued** when submitted. Query its latest status after
rendering to see applied/rejected/superseded and the generation. A Manual owner
can reject an Auto seed at the frame boundary; a successful queue call does not
override that contract. Console execution uses the engine thread, like the UI;
it does not add a parallel mutation service.

## LightBench actions

| Command                  | Effect                                                                         |
| ------------------------ | ------------------------------------------------------------------------------ |
| `lightbench.presets`     | List all preset IDs.                                                           |
| `lightbench.preset`      | Report active/pending preset and whether authored values differ from defaults. |
| `lightbench.preset <id>` | Queue the same complete preset operation as the top-center selector.           |
| `lightbench.reset`       | Queue Reset for the currently active preset.                                   |

Preset operations apply at the next frame boundary. Commands queued in one
frame follow the application's existing last-request-wins behavior; query again
after the next frame before targeting its new scene or exposure owner.
Commands unregister with their owning demo. They do not create a new persistence
format: scene/preferences behavior is the same as edits made through the panels.

## Manual console acceptance

1. In LightBench, `pp.targets` and `pp.inspect` show the current scene and owner.
2. Set Manual EV/compensation, then inspect the Post Process panel; edit the
   panel and inspect again. Both surfaces must agree.
3. Submit an invalid multi-field exposure request; values and the scene must
   remain unchanged. Test an invalid camera triple and gamma pair similarly.
4. Switch to Auto, queue a seed/remeter, then query status after a frame. Switch
   to Manual and check that a seed reports its actual rejection.
5. Select a preset through the console, wait a frame, and use the previous scene
   token: it must reject as stale. The current target and Reset must work.

Native checks cover parsing/atomic rejection, target lifetime, command ownership,
automation/Release policy and token ordering. Existing renderer tests remain the
authority for GPU transition and mask-completion semantics.
