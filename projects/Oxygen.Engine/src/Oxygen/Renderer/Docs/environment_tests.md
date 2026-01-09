# Environment Systems - Visual Test Scenarios

This document provides exhaustive manual test scenarios for the Environment
Systems and their Debug UI in the RenderScene example. Each scenario describes
what to do, what to look for, and the expected visual result.

---

## Prerequisites

- Build and run the **RenderScene** example
- Load a scene with environment systems (or create one via the UI)
- Open the **Environment Systems** debug panel

## Note on Removed Systems

**Fog System**: The standalone Fog UI has been **removed**. For distance-based
atmospheric effects, use **SkyAtmosphere with Aerial Perspective** enabled.
Aerial Perspective provides physically-based Rayleigh/Mie scattering that
naturally fades distant objects into atmospheric haze.

**IBL (SkyLight)**: The SkyLight system is **not yet implemented**. The UI
exists but requires cubemap infrastructure (sky capture or cubemap loading)
that is not available.

---

## 1. Sky Atmosphere System

### 1.1 Basic Enable/Disable - DONE

| # | Action | Expected Result |
| - | ------ | --------------- |
| 1.1.1 | With SkyAtmosphere enabled, check "Enabled" checkbox is ON | Checkbox is checked, atmosphere is rendered |
| 1.1.2 | Uncheck "Enabled" checkbox | Sky turns black/cleared, no atmosphere gradient visible |
| 1.1.3 | Re-enable the checkbox | Atmosphere reappears with previous parameters |

### 1.2 Planet Parameters - DONE

**Note**: From a ground-level observer, the geometric horizon is always
horizontal regardless of planet size. What changes with planet radius is:

- How thick the atmosphere appears relative to the planet
- The rate of density falloff with viewing angle
- Visible planet curvature only appears at significant altitude (Z > 10km)

For testing at ground level, focus on atmosphere thickness and color gradient
changes rather than horizon curvature.

| # | Action | Expected Result |
| - | ------ | --------------- |
| 1.2.1 | Decrease "Radius (km)" to 100 | Atmosphere appears much thicker relative to tiny planet; sky gradient steeper near horizon |
| 1.2.2 | Increase "Radius (km)" to 10000 | Atmosphere appears thin band; more gradual sky gradient |
| 1.2.3 | Increase "Atmo Height (km)" from 80 to 200 | Thicker atmospheric gradient, more gradual color transition |
| 1.2.4 | Decrease "Atmo Height (km)" to 20 | Thin sharp atmosphere layer, abrupt transition to space |
| 1.2.5 | Change "Ground Albedo" to bright green (0.2, 0.8, 0.2) | Ground-bounce lighting takes green tint, visible near horizon |
| 1.2.6 | Set "Ground Albedo" to black (0, 0, 0) | Minimal ground-bounce contribution, darker horizon |

### 1.3 Rayleigh Scattering - DONE

| # | Action | Expected Result |
| - | ------ | --------------- |
| 1.3.1 | Increase "Rayleigh Scale H (km)" from 8 to 20 | Blue sky color extends higher, smoother gradient |
| 1.3.2 | Decrease "Rayleigh Scale H (km)" to 2 | Blue concentrated near horizon, rapid falloff |

### 1.4 Mie Scattering - DONE

**Note**: The Mie phase function controls the angular distribution of scattered light, not the total amount. At high anisotropy (g→1), scattered light is concentrated in a forward cone, creating a visible sun halo. At low anisotropy (g→0), light is distributed equally in all directions, which paradoxically makes the scattering *less* visible per steradian (same total light spread over more directions).

| # | Action | Expected Result |
| - | ------ | --------------- |
| 1.4.1 | Increase "Mie Scale H (km)" from 1.2 to 5 | Haze extends higher into atmosphere |
| 1.4.2 | Decrease "Mie Scale H (km)" to 0.3 | Haze concentrated near ground level |
| 1.4.3 | Set "Mie Anisotropy" to 0.8 (default, forward) | Visible sun halo, typical atmospheric look |
| 1.4.4 | Set "Mie Anisotropy" to 0.99 (extreme forward) | Very bright, tight halo around sun disk |
| 1.4.5 | Set "Mie Anisotropy" to 0.5 (moderate forward) | Broader, dimmer halo around sun |
| 1.4.6 | Set "Mie Anisotropy" to 0.0 (isotropic) | No distinct halo, subtle uniform haze (may be hard to see) |
| 1.4.7 | Set "Mie Anisotropy" to -0.5 (back scatter) | Faint glow when looking away from sun (unusual) |

### 1.5 Multi-Scattering - DONE

| # | Action | Expected Result |
| - | ------ | --------------- |
| 1.5.1 | Set "Multi-Scattering" to 0.0 | Darker shadows, more contrast in sky |
| 1.5.2 | Set "Multi-Scattering" to 1.0 | Softer shadows, brighter overall sky illumination |

### 1.6 Sun Disk - DONE

| # | Action | Expected Result |
| - | ------ | --------------- |
| 1.6.1 | Uncheck "Show Sun Disk" | Sun disk disappears, only atmospheric glow remains |
| 1.6.2 | Re-enable "Show Sun Disk" | Bright sun disk reappears |
| 1.6.3 | Increase "Angular Radius (deg)" from 0.268 to 2.0 | Sun disk becomes much larger |
| 1.6.4 | Decrease "Angular Radius (deg)" to 0.1 | Sun disk becomes tiny pinpoint |

### 1.7 Aerial Perspective - DONE

| # | Action | Expected Result |
| - | ------ | --------------- |
| 1.7.1 | Set "Distance Scale" to 0.0 | No aerial perspective fog on distant objects |
| 1.7.2 | Set "Distance Scale" to 1.0 (default) | Distant objects fade into atmospheric haze |
| 1.7.3 | Set "Distance Scale" to 5.0 | Very strong fog, even nearby objects show haze |

### 1.8 Aerial Perspective Mode - DONE

| # | Action | Expected Result |
| - | ------ | --------------- |
| 1.8.1 | Select "Enabled" radio button | Aerial perspective uses precomputed LUT (higher quality) |
| 1.8.2 | Select "Disabled" radio button | No aerial perspective at all |

### 1.9 LUT Status Verification - DONE

| # | Action | Expected Result |
| - | ------ | --------------- |
| 1.9.1 | Check "Atmosphere LUTs:" status at top of panel | Shows "Valid" in green when LUTs generated |

---

## 2. Sky Sphere System

### 2.1 Basic Enable/Disable - DONE

| # | Action | Expected Result |
| - | ------ | --------------- |
| 2.1.1 | Enable SkySphere while SkyAtmosphere is enabled | Warning displayed: "SkyAtmosphere takes priority" |
| 2.1.2 | Disable SkyAtmosphere, enable SkySphere | SkySphere becomes visible, warning disappears |
| 2.1.3 | Toggle SkySphere enabled/disabled | Sky switches between SkySphere content and black |

### 2.2 Source Selection - PARTIAL

**Note**: Cubemap source is not yet implemented. Use Solid Color for testing.

| # | Action | Expected Result |
| - | ------ | --------------- |
| 2.2.1 | Select "Cubemap" source | Shows warning: "Cubemap source not yet implemented" |
| 2.2.2 | Select "Solid Color" source | Background becomes the solid color, color picker appears |

### 2.3 Solid Color Mode - DONE

| # | Action | Expected Result |
| - | ------ | --------------- |
| 2.3.1 | Set solid color to bright red (1, 0, 0) | Entire sky background is red |
| 2.3.2 | Set solid color to gradient-like blue (0.2, 0.3, 0.5) | Sky is uniform blue-ish color |

### 2.4 Intensity - DONE

| # | Action | Expected Result |
| - | ------ | --------------- |
| 2.4.1 | Set "Intensity" to 0.0 | Sky becomes completely black |
| 2.4.2 | Set "Intensity" to 1.0 (default) | Normal brightness |
| 2.4.3 | Set "Intensity" to 5.0 | Sky becomes very bright, may bloom |

### 2.5 Rotation - DEPENDES ON CUBEMAP

| # | Action | Expected Result |
| - | ------ | --------------- |
| 2.5.1 | Drag "Rotation (deg)" slider | Cubemap rotates around vertical axis (visible if using cubemap) |
| 2.5.2 | Set rotation to 180 degrees | Cubemap appears rotated 180° from original |

---

## 3. Sky Light (IBL) System - NOT IMPLEMENTED

### 3.1 Basic Enable/Disable

| # | Action | Expected Result |
| - | ------ | --------------- |
| 3.1.1 | Enable SkyLight | Objects receive ambient IBL lighting |
| 3.1.2 | Disable SkyLight | Objects lose ambient fill, become darker in shadows |

### 3.2 Source Selection

| # | Action | Expected Result |
| - | ------ | --------------- |
| 3.2.1 | Select "Captured Scene" | IBL derived from current sky (atmosphere or sphere) |
| 3.2.2 | Select "Specified Cubemap" | IBL uses specified cubemap asset |

### 3.3 Tint

| # | Action | Expected Result |
| - | ------ | --------------- |
| 3.3.1 | Set tint to orange (1.0, 0.5, 0.2) | Ambient lighting takes orange tint |
| 3.3.2 | Set tint to white (1, 1, 1) | Neutral ambient lighting |
| 3.3.3 | Set tint to dark (0.1, 0.1, 0.1) | Very dim ambient contribution |

### 3.4 Intensity

| # | Action | Expected Result |
| - | ------ | --------------- |
| 3.4.1 | Set "Intensity" to 0.0 | No IBL contribution at all |
| 3.4.2 | Set "Intensity" to 1.0 | Normal IBL brightness |
| 3.4.3 | Set "Intensity" to 3.0 | Strong ambient fill, may wash out details |

### 3.5 Diffuse/Specular Balance

| # | Action | Expected Result |
| - | ------ | --------------- |
| 3.5.1 | Set "Diffuse" to 0, "Specular" to 1 | Only specular reflections, no diffuse fill |
| 3.5.2 | Set "Diffuse" to 1, "Specular" to 0 | Only diffuse fill, no specular reflections |
| 3.5.3 | Set both to 1.0 | Full diffuse and specular IBL |
| 3.5.4 | Set "Diffuse" to 2.0 | Enhanced diffuse, may look unnaturally flat |

---

## 5. Post Process Volume - NOT IMPLEMENTED

### 5.1 Basic Enable/Disable

| # | Action | Expected Result |
| - | ------ | --------------- |
| 5.1.1 | Enable PostProcess | Post processing effects applied |
| 5.1.2 | Disable PostProcess | Raw HDR output, may look washed out |

### 5.2 Tonemapping

| # | Action | Expected Result |
| - | ------ | --------------- |
| 5.2.1 | Select "ACES Fitted" | Filmic tonemapping, natural contrast |
| 5.2.2 | Select "Reinhard" | Softer contrast, more vintage look |
| 5.2.3 | Select "None" | Linear output, likely over-bright or washed out |

### 5.3 Exposure Mode

| # | Action | Expected Result |
| - | ------ | --------------- |
| 5.3.1 | Select "Manual" exposure | Fixed exposure regardless of scene brightness |
| 5.3.2 | Select "Auto" exposure | Camera auto-adjusts to scene brightness |

### 5.4 Exposure Compensation

| # | Action | Expected Result |
| - | ------ | --------------- |
| 5.4.1 | Set "Compensation (EV)" to -2 | Scene becomes 2 stops darker |
| 5.4.2 | Set "Compensation (EV)" to 0 | Neutral exposure |
| 5.4.3 | Set "Compensation (EV)" to +2 | Scene becomes 2 stops brighter |

### 5.5 Auto-Exposure Parameters

| # | Action | Expected Result |
| - | ------ | --------------- |
| 5.5.1 | In Auto mode, set "Min EV" to 0 | Auto-exposure never goes below EV 0 |
| 5.5.2 | Set "Max EV" to 10 | Auto-exposure clamps at EV 10 |
| 5.5.3 | Set "Speed Up" to 0.5 | Slow adaptation when brightening |
| 5.5.4 | Set "Speed Up" to 10 | Fast adaptation when brightening |
| 5.5.5 | Set "Speed Down" to 0.5 | Slow adaptation when darkening |
| 5.5.6 | Set "Speed Down" to 5 | Fast adaptation when darkening |
| 5.5.7 | Look at bright area then dark area | Observe adaptation speed |

### 5.6 Bloom

| # | Action | Expected Result |
| - | ------ | --------------- |
| 5.6.1 | Set "Intensity (Bloom)" to 0.0 | No bloom effect |
| 5.6.2 | Set "Intensity (Bloom)" to 0.5 | Moderate glow around bright areas |
| 5.6.3 | Set "Intensity (Bloom)" to 2.0 | Strong bloom, dreamlike quality |
| 5.6.4 | Set "Threshold" to 0.5 | Bloom begins at lower brightness |
| 5.6.5 | Set "Threshold" to 5.0 | Only very bright areas bloom |

### 5.7 Color Grading

| # | Action | Expected Result |
| - | ------ | --------------- |
| 5.7.1 | Set "Saturation" to 0.0 | Grayscale image |
| 5.7.2 | Set "Saturation" to 1.0 | Normal color |
| 5.7.3 | Set "Saturation" to 2.0 | Over-saturated, vibrant colors |
| 5.7.4 | Set "Contrast" to 0.5 | Flat, low-contrast image |
| 5.7.5 | Set "Contrast" to 1.0 | Normal contrast |
| 5.7.6 | Set "Contrast" to 1.5 | High contrast, punchy look |
| 5.7.7 | Set "Vignette" to 0.0 | No vignette |
| 5.7.8 | Set "Vignette" to 0.5 | Moderate darkening at edges |
| 5.7.9 | Set "Vignette" to 1.0 | Strong vignette, tunnel effect |

---

## 6. Sun Light Override - DONE

**Important Conceptual Separation**:

- **Sun Light** = A DirectionalLight with intensity and color that illuminates scene objects
- **Environment Illuminance** = Derived value (intensity × peak_color) used by SkyAtmosphere for sky brightness

The Sun Light Override controls the DirectionalLight properties, NOT environment-specific settings.

### 6.1 Enable/Disable

| # | Action | Expected Result |
| - | ------ | --------------- |
| 6.1.1 | Check "Enable Sun Override" | Overrides scene sun with manual direction, intensity, color |
| 6.1.2 | Uncheck "Enable Sun Override" | Scene uses actual DirectionalLight from scene |

### 6.2 Direction Control

| # | Action | Expected Result |
| - | ------ | --------------- |
| 6.2.1 | Set "Azimuth (deg)" to 0 | Sun direction at +X axis |
| 6.2.2 | Set "Azimuth (deg)" to 90 | Sun direction at +Z axis |
| 6.2.3 | Set "Azimuth (deg)" to 180 | Sun direction at -X axis |
| 6.2.4 | Drag azimuth continuously | Sun rotates around sky smoothly and continuously |
| 6.2.5 | Set "Elevation (deg)" to 0 | Sun at horizon level |
| 6.2.6 | Set "Elevation (deg)" to 45 | Sun halfway up the sky |
| 6.2.7 | Set "Elevation (deg)" to 90 | Sun directly overhead |
| 6.2.8 | Set "Elevation (deg)" to -10 | Sun below horizon (night/twilight) |
| 6.2.9 | Drag elevation continuously | Sun moves smoothly up/down without jumps |

### 6.3 Intensity (Light Strength)

| # | Action | Expected Result |
| - | ------ | --------------- |
| 6.3.1 | Set "Intensity" to 0 | No direct sun lighting on objects |
| 6.3.2 | Set "Intensity" to 10 | Moderate sun lighting |
| 6.3.3 | Set "Intensity" to 50 | Bright daylight intensity |
| 6.3.4 | Set "Intensity" to 100 | Maximum sun intensity |
| 6.3.5 | Drag intensity continuously | Brightness changes smoothly and continuously |

### 6.4 Color (Light Tint)

| # | Action | Expected Result |
| - | ------ | --------------- |
| 6.4.1 | Set color to pure white (1,1,1) | Neutral white sunlight |
| 6.4.2 | Set color to warm orange (1,0.8,0.6) | Sunset/sunrise colored lighting |
| 6.4.3 | Set color to cool blue (0.8,0.9,1) | Cold overcast lighting |
| 6.4.4 | Pick various colors in color picker | Light tint changes on all lit surfaces |

### 6.5 Combined Effects

| # | Action | Expected Result |
| - | ------ | --------------- |
| 6.5.1 | Set low elevation + warm color | Sunrise/sunset look |
| 6.5.2 | Set high elevation + high intensity | Midday harsh sunlight |
| 6.5.3 | Set below-horizon elevation + SkyAtmosphere | Night sky, no sun disk |
| 6.5.4 | Verify objects receive proper lighting | Diffuse and specular respond to sun changes |

### 6.6 Direction Display

| # | Action | Expected Result |
| - | ------ | --------------- |
| 6.6.1 | Observe "Direction: (x, y, z)" display | Shows computed normalized direction vector |
| 6.6.2 | Verify direction changes with azimuth/elevation | Values update in real-time |

---

## 7. System Interactions - DONE

### 7.1 SkyAtmosphere + SkySphere Mutual Exclusion

| # | Action | Expected Result |
| - | ------ | --------------- |
| 7.1.1 | Enable SkyAtmosphere when SkySphere is enabled | SkySphere auto-disables |
| 7.1.2 | Enable SkySphere when SkyAtmosphere is enabled | SkyAtmosphere auto-disables |
| 7.1.3 | Manually enable both (if possible) | Warning appears, SkyAtmosphere takes priority |
| 7.1.4 | Add SkyAtmosphere when SkySphere exists | SkySphere auto-disables |
| 7.1.5 | Add SkySphere when SkyAtmosphere exists | SkyAtmosphere auto-disables |

### 7.2 SkyLight + Sky Source

| # | Action | Expected Result |
| - | ------ | --------------- |
| 7.2.1 | SkyLight "Captured Scene" with SkyAtmosphere | IBL matches atmospheric colors |
| 7.2.2 | SkyLight "Captured Scene" with SkySphere | IBL matches sky sphere content |
| 7.2.3 | Change sky, observe IBL update | Objects reflect new sky ambient |

### 7.3 Sun Direction Consistency

| # | Action | Expected Result |
| - | ------ | --------------- |
| 7.3.1 | Move sun with override | Atmosphere sun disk follows override direction |
| 7.3.2 | Check shadow directions | Shadows match sun override direction |
