# EX09 preset visual review

Reviewed 2026-09-25 in the existing **non-Tracy Ninja Release** build. All seven
presets were launched separately, captured from their visible 1920×1080 client
areas and inspected by the agent, as requested. Manual scenes settled for five
seconds; Auto scenes for eighteen seconds. Each process closed normally with
exit 0. Adjacent JSON files record the capture method, dimensions and preset;
logs contain no reported runtime warning/error. These are visual checks, not
performance measurements or renewed EX07 baselines.

The seven images below record the earlier preset appearance review. The user
subsequently rejected the reserved panel viewport. Final LightBench restores
full-window rendering with translucent overlays and defaults that render objects
20% smaller. See [final overlay evidence](EX09-completion.md); these earlier images are
not final UI acceptance. Their photometric findings remain historical evidence.

## Findings and corrections

- Removed the paragraphs describing each preset from the panel. The compact
  selector, reset/status row and controls remain; explanations are in the README.
- Reduced field widths and shortened unit labels to prevent clipped controls.
  Enabled sources appear first, so Point and Spot presets expose their controls
  immediately.
- Lowered the viewing-scene cards and spheres onto the floor. Previously their
  centers were one metre high despite their half-height/radius being 0.5 m.
- Changed directional incidence in the viewing scenes so the gray card remains
  distinguishable from the gray floor. The calibrated material values and
  Neutral/Point/Spot reference scenes are unchanged.
- Enabled both local-light shadow flags explicitly in Indoor. Previously the
  scene inherited the engine's disabled default. Point/spot shadow switches are
  now available alongside Enabled, and save/load preserves them.

## Image judgments

| Preset and screenshot                                                               | Observed result                                                                                                                                      | Judgment                                                                                                |
| ----------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| [Neutral Reference](validation/ex09-presets-20260925/screens/neutral-reference.png) | Three centered cards with distinct gray/white/black responses; compact reference table.                                                              | Suitable for the numerical reference.                                                                   |
| [Point Falloff](validation/ex09-presets-20260925/screens/point-falloff.png)         | Visible radial falloff on a broad gray receiver; distance buttons and active point controls accessible.                                              | Suitable for fixed-exposure distance comparison.                                                        |
| [Spot Cone](validation/ex09-presets-20260925/screens/spot-cone.png)                 | Clear footprint, bright center and visible edge falloff; active spot controls accessible.                                                            | Full footprint independently checked in both render paths; see below.                                   |
| [Material Lighting](validation/ex09-presets-20260925/screens/material-lighting.png) | Grounded spheres, distinct matte/glossy responses, small glossy highlight and visible cast shadows.                                                  | Suitable for direct-light material comparison.                                                          |
| [Auto Adaptation](validation/ex09-presets-20260925/screens/auto-adaptation.png)     | All cards remain distinct after settling; objects and shadows remain readable.                                                                       | Suitable starting view for adaptation steps; native tests separately verify both transition directions. |
| [Indoor](validation/ex09-presets-20260925/screens/indoor.png)                       | Warm local illumination, distinct cards, readable sphere shading and visible overlapping local shadows.                                              | Usable; shadow edges show visible stair-stepping that warrants a separate quality assessment.           |
| [Outdoor Daylight](validation/ex09-presets-20260925/screens/outdoor-daylight.png)   | White directional illumination, distinct cards and cast shadows. Settled brightness resembles the lower-lux Auto scene because exposure compensates. | Suitable daylight comparison.                                                                           |

Indoor and Outdoor are direct-lighting calibration rigs, not complete room/sky/GI
environments. Deep unlit regions are expected with environmental lighting
disabled. Source review confirms that Indoor uses the ordinary Medium per-light resolution
hint, requesting 1024-pixel local maps, and the existing production filters.
The screenshot exposes finite-map edge quality; it does not establish a new
filter correctness defect or justify arbitrary bias tuning. The known EX07 PCF
qualification remains applicable because its shader/settings contract is unchanged.
Final acceptance of this viewing quality remains with the user; keep the image
and do not hide the limitation by disabling shadows.

Earlier placement images are retained under `screens/before-placement/` for
comparison. The screenshots demonstrate scene appearance and panel layout;
they do not prove every camera/edit/save/load interaction. Final source review,
remaining user interaction acceptance and EX09D/E are tracked separately. No
overall EX09 closure or commit approval is implied.

## Spot Cone follow-up: full falloff check

The user questioned the visible inner transition. The original two-sample test
only qualified center brightness and darkness outside the cone; that was not
full-profile proof. The additional native test now checks **31,625 foreground
pixels per rendering path**, including **7,924 outside-cone pixels** required to
remain exactly black. Both spot tests pass in **Debug and Release (2/2 each)**
using the existing non-Tracy Ninja tree. No renderer or preset changes were
needed, and the existing screenshot was reused.

The independent CPU calculation combines solid-angle flux normalization,
inverse-square attenuation, authored range fade, receiver cosine and the EX07
GGX reference. It includes the complete disk and an unlit surrounding annulus.
The footprint error budget is 2e-5 of peak HDR, with the existing deferred
material storage/sampling bounds added separately. The original stricter
center-relative photometry test remains in place.

| Quantity                                    |                                                                                  Result |
| ------------------------------------------- | --------------------------------------------------------------------------------------: |
| Expected peak luminance                     |                                                                        15.7317796 cd/m² |
| Inner boundary: 3 × tan(15°)                |                                                                              0.803848 m |
| Outer boundary: 3 × tan(30°)                |                                                                              1.732051 m |
| Forward maximum absolute HDR error          |                                                    0.00005937 cd/m² (0.000377% of peak) |
| Deferred maximum absolute HDR error         |                                         0.1323704 cd/m², within material packing bounds |
| Deferred maximum excess over packing bounds |                                                                     0.00000003432 cd/m² |
| Saved screenshot versus ideal display curve | Mean 0.3261, maximum 1.5406 code values out of 255 over 502,652 pixels/all RGB channels |

The screenshot differences include material packing, dithering, display
quantization and pixel registration; the native HDR test is the numerical
acceptance gate. The screenshot curve closely follows the independent prediction:

![Captured radial falloff versus independent model](validation/ex09-presets-20260925/spot-capture-profile.png)

**Conclusion:** the visible inner ring is the expected change in slope where the
angular response leaves its constant inner region and starts its squared
cosine-space falloff. Brightness is continuous across the boundary. It is not
saturation, an Auto exposure effect, or an unexplained discontinuity in shading.
A smoother transition would require intentionally changing the authored angular
profile and its flux normalization; this check does not justify such a change.

[Test evidence/hashes](validation/ex09-presets-20260925/spot-profile-summary.json),
[Release results](validation/ex09-presets-20260925/lightbench-spot-profile-release.json.gz),
[Debug results](validation/ex09-presets-20260925/lightbench-spot-profile-debug.json.gz),
[reproducible screenshot calculation](validation/ex09-presets-20260925/check_spot_capture.py).

## Permanent preset bar: bounded visual polish

At the user's request, preset selection, Reset, Defaults/Modified and contextual
buttons now live in a separate, fixed top-center overlay. It remains available
while any sidebar panel is active. The full-window scene and translucent sidebar
contract are unchanged. Only the preset bar's local ImGui styles are adjusted.

Final Release screenshots at 1920×1080 show a **410×117 px** bar, centered with
**23 px top margin**. Font-relative sizing/padding are snapped to whole pixels;
rounded corners, no title bar/border, stronger status contrast and equal-width
context buttons keep it compact. Blue selection persists while hovering/clicking.
The popup minimum height accounts for all seven rows and padding; it no longer
shows the tiny overflow scrollbar seen in the first attempt.

| Step               | Screenshot                                                                                      | Judgment                                                                                     |
| ------------------ | ----------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| 1. Default bar     | [Point Falloff](validation/ex09-completion-20260925/preset-overlay-final/point-falloff.png)     | Compact, centered, padded; 2 m selected and Defaults readable.                               |
| 2. Open selector   | [Seven options](validation/ex09-completion-20260925/preset-overlay-final/02-preset-menu.png)    | All labels visible with no scrollbar or cropped final option.                                |
| 3. Switch sidebar  | [Camera controls](validation/ex09-completion-20260925/preset-overlay-final/03-camera-panel.png) | Preset bar remains accessible at the same position; scene viewport stays fixed.              |
| 4. Change distance | [1 m selected](validation/ex09-completion-20260925/preset-overlay-final/04-dim-selected.png)    | Click changes illumination, selected button stays blue under hover, status becomes Modified. |
| 5. Reset           | [Restored default](validation/ex09-completion-20260925/preset-overlay-final/05-reset.png)       | Returns to 2 m and Defaults without changing the active Camera panel.                        |

Both Debug and Release app builds pass. This presentation-only follow-up reuses
the qualified scene/photometry tests; it does not rerun performance baselines.
Mouse actions and screenshot states were inspected. Complete keyboard/focus
regression coverage remains the separately scheduled EX08.2 work. The user's
"OMG it's nice" feedback approves the visual direction; it does not by itself
close the remaining EX09 interaction and MultiView acceptance gates.

[Current UI identities and screenshot hashes](validation/ex09-completion-20260925/preset-overlay-final/review.json).

### Final status-indicator change

The user requested removing Defaults/Modified text entirely. The final bar is
narrower and uses only a thin amber border for dirty configuration; the normal
(default) state has no visible border. Context buttons fill the available row.
Reset restores the preset and clears the outline. This supersedes the textual
status presentation in the preceding screenshot set. Current clean/dirty/reset
images are under `validation/ex09-completion-20260925/preset-border/`.

The final dirty highlight uses the existing Spectrum orange palette token,
separating modified configuration from blue selected controls.

Verified final states: [modified with amber border](validation/ex09-completion-20260925/preset-border/04-dim-selected.png)
and [Reset restored, no border](validation/ex09-completion-20260925/preset-border/05-reset.png).
Both labels are absent; the active button remains blue. Debug and Release builds
pass. [Final indicator evidence](validation/ex09-completion-20260925/preset-border/review.json).

### Popup border isolation

The user's open-popup check found the dirty color leaking into the selector's
border, including while the preset itself was clean. The parent window's border
style is now popped immediately after `Begin` renders its decoration, before
creating any popup. Actual captures verify a neutral dropdown in both
[clean](validation/ex09-completion-20260925/popup-border-scope/02-preset-menu.png)
and [dirty](validation/ex09-completion-20260925/popup-border-scope/04b-dirty-popup.png)
states; only the dirty parent retains amber. All seven entries remain visible.
[Current source and image identities](validation/ex09-completion-20260925/popup-border-scope/review.json).
