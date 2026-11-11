# Physics Domains Demo Scene

This directory is the canonical physics-sidecar authoring sample for
Oxygen Engine. It demonstrates **all seven sidecar binding families**
in a single scene with scripted controls and runtime hydration.

## What It Shows

| Family           | Objects                                                                          | Highlight                                                          |
| ---------------- | -------------------------------------------------------------------------------- | ------------------------------------------------------------------ |
| **Rigid bodies** | Floor, ramp, bouncy sphere, hinge pair, 4-cube stack, vehicle chassis + 4 wheels | Mixed static/dynamic, `linear_cast` motion quality for fast sphere |
| **Colliders**    | Trigger zone                                                                     | Invisible sensor volume in path of sphere                          |
| **Characters**   | Character capsule                                                                | 90 kg character with slope + step limits                           |
| **Soft bodies**  | Jelly sphere                                                                     | Drops from height; edge/shear/bend compliance visible              |
| **Joints**       | Hinge joint A↔B                                                                  | Two red cubes linked by a hinge constraint                         |
| **Vehicles**     | 4-wheel chassis                                                                  | Green box with 4 dark cylinder wheels                              |
| **Aggregates**   | Anchor node                                                                      | 16-body aggregate surrounding the hinge pair                       |

It also cooks every collision shape type the runtime builds from a cooked
payload or a compound. Each dynamic one starts tilted 10–16 m up and lands
on one of the new static shapes: the cone tips off the pedestal, the
bouncy hull rock bounces off it, and the dumbbell and a ball roll down
the sloped terrain patch. The terrain's render mesh is generated from the
same samples (`Tools/Generators/TerrainPatch.py`), so it is drawn where
bodies rest:

| Shape         | Object                   | Cooked as                                                                     |
| ------------- | ------------------------ | ----------------------------------------------------------------------------- |
| Cone          | Cone spire (5.6, 4.5)    | Descriptor parameters; the runtime builds it                                  |
| Convex hull   | Hull rock (5, 4.5)       | Jolt hull of `hull_rock.ogeo`                                                 |
| Triangle mesh | Static pedestal (5, 4.5) | Jolt mesh of `pedestal.ogeo`                                                  |
| Compound      | Dumbbell (2.6, -9.6)     | Child records: a hull child with its own payload, and a cone child            |
| Height field  | Terrain patch (1, -13)   | Jolt height field of 13 × 13 authored samples with a hole; a ball lands on it |

## Scene Layout (top-down, Z-up)

```text
              +X
               |
   Ramp ──→   ●  BouncySphere (drops onto ramp, bounces into stack)
   (-4,0)     |
              |
  Character   |        Hinge pair (A↔B)    Stack (4 cubes)
  (-2,-3)     |        (2.5, 2.5)          (3, -2)
              |
              |     Vehicle (0, -4)
              |      [FL] [FR]
              |      [RL] [RR]
              |
              +──────────────── -Y (forward)
```

## Import Domains

1. `physics-material-descriptor` — `.opmat` (ground, dynamic, bouncy)
2. `collision-shape-descriptor` — `.ocshape` (box, sphere, cylinder, capsule,
   cone, convex hull, triangle mesh, compound, height field)
3. `physics-sidecar` — `.opscene` (bindings across all seven families)

## Files

### Render Materials

- `chassis_green.omat.json` — Dark green metallic for vehicle chassis
- `wheel_dark.omat.json` — Dark rubber for wheels
- `ramp_orange.omat.json` — Orange for ramp
- `softbody_pink.omat.json` — Pink for soft body sphere
- `character_teal.omat.json` — Teal for character capsule
- `stack_yellow.omat.json` — Yellow for stacked cubes
- `terrain_moss.omat.json` — Moss green for the terrain patch

### Render Geometries

- `chassis_box.ogeo.json` — Procedural cube scaled as chassis
- `cylinder_wheel.ogeo.json` — Procedural cylinder for wheels
- `ramp.ogeo.json` — Procedural cube scaled as ramp
- `character_capsule.ogeo.json` — Z-axis capsule matching `actor.shape.json`:
  radius 0.35 m, cylindrical half-height 0.9 m, total height 2.5 m. Physics uses
  cylindrical half-height; the render recipe uses total height including caps.
- `softbody_sphere.ogeo.json` — Procedural sphere for soft body
- `stack_cube.ogeo.json` — Procedural cube for stack elements
- `cone_spire.ogeo.json` — Procedural cone (r=0.4, height 1.2), apex up
- `hull_rock.ogeo.json` — Procedural icosphere, the hull rock's points
- `pedestal.ogeo.json` — Procedural 12-sided cylinder, the pedestal's mesh
- `terrain.ogeo.json` and `terrain.buffers.json` — The terrain's mesh, its
  buffers in `SourceMedia/DCC/PhysicsDomains/`; regenerate all three with
  `python Tools/Generators/TerrainPatch.py` after changing
  `terrain.shape.json`

### Physics Shapes

- `floor.shape.json` — Box (10×10×0.5), ground material
- `chassis.shape.json` — Box (0.9×1.8×0.3), dynamic material
- `wheel.shape.json` — Cylinder (r=0.3, hh=0.15), dynamic material
- `ramp.shape.json` — Box (2×3×0.15), ground material
- `stack_cube.shape.json` — Box (0.4³), dynamic material
- `trigger_zone.shape.json` — Box (2×2×1.5), sensor
- `sphere_bouncy.shape.json` — Sphere (r=1.0), bouncy material
- `actor.shape.json` — Capsule (r=0.35, hh=0.9), dynamic material
- `cone_spire.shape.json` — Cone (r=0.4, hh=0.6), dynamic material
- `hull_rock.shape.json` — Convex hull of `hull_rock.ogeo`, bouncy material
- `pedestal.shape.json` — Triangle mesh of `pedestal.ogeo`, ground material
- `dumbbell.shape.json` — Compound of a half-scale hull rock and a cone joined
  by a box bar
- `terrain.shape.json` — Sloped height field, 13 × 13 samples 0.6 m apart,
  one hole
- `terrain_ball.shape.json` — Sphere (r=0.5), dynamic material

### Physics Materials

- `ground.physics-material.json` — High friction, low bounce
- `dynamic.physics-material.json` — Medium friction, low bounce
- `bouncy.physics-material.json` — Low friction, high restitution (0.85)

### Scene & Sidecar

- `PhysicsDomains.scene.json` — 32-node scene graph
- `PhysicsDomains.physics-sidecar.json` — 7-family sidecar bindings
- `Manifests/PhysicsDomains.import-manifest.json` — the example's import
  manifest (the benchmark variant has its own, `PhysicsDomainsVsmBenchmark`)

## Run

From `projects/Oxygen.Engine`, follow the [content workflow](../../../README.md):

```powershell
Examples/RenderScene/Tools/cook PhysicsDomains
Examples/RenderScene/Tools/play PhysicsDomains
```

Or open RenderScene, select `PhysicsDomains` in Library, and use **Use Scene** to
inspect authored lighting. Check the load diagnostics for completed physics
sidecar hydration before assessing bodies, joints or vehicle motion. The
[RenderScene guide](../../../../RenderScene/README.md) covers launch and verification.
