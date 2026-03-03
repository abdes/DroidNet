# Physics Domains Demo Scene

This directory is the canonical physics-sidecar authoring sample for
Oxygen Engine. It demonstrates **all seven sidecar binding families**
in a single, visually interesting scene.

## What It Shows

| Family | Objects | Highlight |
|--------|---------|-----------|
| **Rigid bodies** | Floor, ramp, bouncy sphere, hinge pair, 4-cube stack, vehicle chassis + 4 wheels | Mixed static/dynamic, `linear_cast` motion quality for fast sphere |
| **Colliders** | Trigger zone | Invisible sensor volume in path of sphere |
| **Characters** | Character capsule | 90 kg character with slope + step limits |
| **Soft bodies** | Jelly sphere | Drops from height; edge/shear/bend compliance visible |
| **Joints** | Hinge joint A↔B | Two red cubes linked by a hinge constraint |
| **Vehicles** | 4-wheel chassis | Green box with 4 dark cylinder wheels |
| **Aggregates** | Anchor node | 16-body aggregate surrounding the hinge pair |

## Scene Layout (top-down, Z-up)

```
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

1. `physics-resource-descriptor` — `.opres` (hinge, vehicle, Jolt soft-body settings, PhysX soft-body settings)
2. `physics-material-descriptor` — `.opmat` (ground, dynamic, bouncy)
3. `collision-shape-descriptor` — `.ocshape` (box, sphere, cylinder, capsule)
4. `physics-sidecar` — `.opscene` (bindings across all seven families)

## Files

### Render Materials

- `chassis_green.material.json` — Dark green metallic for vehicle chassis
- `wheel_dark.material.json` — Dark rubber for wheels
- `ramp_orange.material.json` — Orange for ramp
- `softbody_pink.material.json` — Pink for soft body sphere
- `character_teal.material.json` — Teal for character capsule
- `stack_yellow.material.json` — Yellow for stacked cubes

### Render Geometries

- `chassis_box.geometry.json` — Procedural cube scaled as chassis
- `cylinder_wheel.geometry.json` — Procedural cylinder for wheels
- `ramp.geometry.json` — Procedural cube scaled as ramp
- `character_capsule.geometry.json` — Procedural cylinder for character
- `softbody_sphere.geometry.json` — Procedural sphere for soft body
- `stack_cube.geometry.json` — Procedural cube for stack elements

### Physics Shapes

- `floor.shape.json` — Box (10×10×0.5), ground material
- `chassis.shape.json` — Box (0.9×1.8×0.3), dynamic material
- `wheel.shape.json` — Cylinder (r=0.3, hh=0.15), dynamic material
- `ramp.shape.json` — Box (2×3×0.15), ground material
- `stack_cube.shape.json` — Box (0.4³), dynamic material
- `trigger_zone.shape.json` — Box (2×2×1.5), sensor
- `sphere_bouncy.shape.json` — Sphere (r=0.5), bouncy material
- `actor.shape.json` — Capsule (r=0.35, hh=0.9), dynamic material

### Physics Materials

- `ground.physics-material.json` — High friction, low bounce
- `dynamic.physics-material.json` — Medium friction, low bounce
- `bouncy.physics-material.json` — Low friction, high restitution (0.85)

### Scene & Sidecar

- `physics_domains.scene.json` — 21-node scene graph
- `physics_domains.physics-sidecar.json` — 7-family sidecar bindings
- `import-manifest.json` — 32-job import manifest

### Data

- `data/park_hinge_joint_a.jphbin` — Binary payload for hinge constraint
- `data/park_vehicle_constraint_a.jphbin` — Binary payload for vehicle constraint
- `data/park_soft_body_settings_a.jphbin` — Binary payload reused by both soft-body settings descriptors (Jolt/PhysX format tags)

## Run

From repository root:

```powershell
.\out\build-ninja\bin\Debug\Oxygen.Cooker.ImportTool.exe batch --manifest .\Examples\Content\scenes\physics_domains\import-manifest.json
```

Expected output root: `Examples/Content/.cooked`
